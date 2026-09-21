#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <sys/sysctl.h>
#include <sys/xattr.h>
#include <mach-o/loader.h>

static NSString* Join(NSString* a, NSString* b) { return [a stringByAppendingPathComponent:b]; }
static NSString* DefaultConfig() {
    NSString* xdg = NSProcessInfo.processInfo.environment[@"XDG_CONFIG_HOME"];
    return Join(xdg.length ? xdg : Join(NSHomeDirectory(), @".config"), @"dkr-port");
}
static NSString* DiagnosticRoot() {
    return Join(NSHomeDirectory(), @"Library/Application Support/DKR-R/Diagnostics");
}
static NSString* ReadTail(NSString* path) {
    NSFileHandle* file = [NSFileHandle fileHandleForReadingAtPath:path];
    if (!file) return @"[Unavailable or not yet created]\n";
    @try {
        unsigned long long length = [file seekToEndOfFile];
        [file seekToFileOffset:length > 262144 ? length - 262144 : 0];
        NSData* data = [file readDataOfLength:262144];
        return [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"[Not UTF-8 text]\n";
    } @catch (NSException*) { return @"[Could not read file]\n"; }
    @finally { [file closeFile]; }
}
static NSString* Redact(NSString* text) {
    NSMutableString* result = [NSMutableString string];
    // Log messages can contain arbitrary imported filenames and network data.
    // Omit whole sensitive lines instead of attempting to guess their boundaries.
    NSRegularExpression* sensitive = [NSRegularExpression regularExpressionWithPattern:
        @"(?i)(/Users/|/Volumes/|/private/|/var/|/tmp/|file://|https?://|(?:^|[ =\"(])/(?!/)|[A-Z]:\\\\|@|\\b(?:token|password|secret|invite|invitation|friend|candidate|credential|room|account|username|hostname|serial|cookie)\\b|\\b(?:[0-9]{1,3}\\.){3}[0-9]{1,3}\\b|\\b(?:[0-9a-f]{1,4}:){2,}[0-9a-f]{1,4}\\b|\\b[0-9a-f]{1,4}::[0-9a-f:]+\\b|(?<!\\w)::[0-9a-f]+\\b)"
        options:0 error:nil];
    for (NSString* line in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        if ([sensitive firstMatchInString:line options:0 range:NSMakeRange(0,line.length)])
            [result appendString:@"[Line with personal path or connection data omitted]\n"];
        else [result appendFormat:@"%@\n", line];
    }
    return result;
}
static NSArray<NSString*>* Recent(NSString* directory, NSString* prefix, NSSet* extensions, NSUInteger limit) {
    NSFileManager* fm = NSFileManager.defaultManager;
    NSMutableArray<NSString*>* files = [NSMutableArray array];
    for (NSString* name in [fm contentsOfDirectoryAtPath:directory error:nil]) {
        NSString* path = Join(directory,name);
        NSDictionary* info = [fm attributesOfItemAtPath:path error:nil];
        if ([name hasPrefix:prefix] && [extensions containsObject:name.pathExtension] &&
            [info[NSFileType] isEqual:NSFileTypeRegular] && [info[NSFileSize] unsignedLongLongValue] > 0)
            [files addObject:path];
    }
    [files sortUsingComparator:^NSComparisonResult(NSString* a, NSString* b) {
        NSDate* ad = [fm attributesOfItemAtPath:a error:nil][NSFileModificationDate];
        NSDate* bd = [fm attributesOfItemAtPath:b error:nil][NSFileModificationDate];
        return [bd compare:ad];
    }];
    return [files subarrayWithRange:NSMakeRange(0,MIN(limit,files.count))];
}
static NSString* AppleCrashSummary(NSString* path) {
    // Never export an entire Apple report: it includes user IDs and personal paths.
    NSDictionary* attributes = [NSFileManager.defaultManager attributesOfItemAtPath:path error:nil];
    if ([attributes[NSFileSize] unsignedLongLongValue] > 8*1024*1024) return @"[Report exceeds size limit]\n";
    NSString* text = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    if (!text) return @"[Report unavailable]\n";
    NSMutableString* output = [NSMutableString string];
    if ([path.pathExtension isEqual:@"ips"]) {
        NSRange newline = [text rangeOfString:@"\n"];
        NSString* body = newline.location == NSNotFound ? text : [text substringFromIndex:NSMaxRange(newline)];
        id decoded = [NSJSONSerialization JSONObjectWithData:[body dataUsingEncoding:NSUTF8StringEncoding] options:0 error:nil];
        if (![decoded isKindOfClass:NSDictionary.class]) return @"[Apple report format not recognized]\n";
        NSDictionary* report = decoded;
        for (NSString* group in @[@"osVersion",@"exception",@"termination"]) {
            id fields = report[group];
            if (![fields isKindOfClass:NSDictionary.class]) continue;
            for (NSString* key in @[@"train",@"build",@"type",@"signal",@"codes",@"namespace",@"code",@"indicator"]) {
                id value = fields[key];
                if ([value isKindOfClass:NSString.class] || [value isKindOfClass:NSNumber.class])
                    [output appendFormat:@"%@.%@: %@\n",group,key,value];
            }
        }
        NSArray* threads = [report[@"threads"] isKindOfClass:NSArray.class] ? report[@"threads"] : @[];
        NSArray* images = [report[@"usedImages"] isKindOfClass:NSArray.class] ? report[@"usedImages"] : @[];
        for (id thread in threads) {
            if (![thread isKindOfClass:NSDictionary.class] || ![thread[@"triggered"] isKindOfClass:NSNumber.class] || ![thread[@"triggered"] boolValue]) continue;
            [output appendString:@"Crashed thread frames:\n"];
            id frames = thread[@"frames"];
            if (![frames isKindOfClass:NSArray.class]) continue;
            NSUInteger count=0;
            for (id frame in frames) {
                if (++count>48) break;
                if (![frame isKindOfClass:NSDictionary.class]) continue;
                NSUInteger index = [frame[@"imageIndex"] isKindOfClass:NSNumber.class] ? [frame[@"imageIndex"] unsignedIntegerValue] : NSUIntegerMax;
                id image = index < images.count ? images[index] : @{};
                if (![image isKindOfClass:NSDictionary.class]) image=@{};
                [output appendFormat:@"%@ + %@  %@ + %@ (binary UUID %@)\n",
                    image[@"name"] ?: @"unknown",frame[@"imageOffset"] ?: @0,
                    frame[@"symbol"] ?: @"unsymbolicated",frame[@"symbolLocation"] ?: @0,image[@"uuid"] ?: @"unknown"];
            }
        }
    } else {
        BOOL frames=NO; NSUInteger count=0;
        for (NSString* line in [text componentsSeparatedByString:@"\n"]) {
            if ([line hasPrefix:@"Thread "] && [line containsString:@"Crashed:"]) frames=YES;
            if (frames && !line.length) frames=NO;
            if ((frames && count++<48) || [line hasPrefix:@"Exception Type:"] ||
                [line hasPrefix:@"Exception Codes:"] || [line hasPrefix:@"Termination Reason:"] ||
                [line hasPrefix:@"OS Version:"] || [line hasPrefix:@"Code Type:"])
                [output appendFormat:@"%@\n",line];
        }
    }
    return Redact(output.length ? output : @"[No supported crash fields found]\n");
}
static NSString* RunCheck(NSString* executable, NSArray* args) {
    NSTask* task = [NSTask new]; task.executableURL = [NSURL fileURLWithPath:executable]; task.arguments=args;
    NSPipe* pipe=[NSPipe pipe]; task.standardOutput=pipe; task.standardError=pipe;
    NSError* error=nil;
    if (![task launchAndReturnError:&error]) return @"check unavailable";
    NSDate* deadline=[NSDate dateWithTimeIntervalSinceNow:8];
    while (task.running && deadline.timeIntervalSinceNow>0) [NSThread sleepForTimeInterval:0.02];
    if (task.running) { [task terminate]; return @"check timed out"; }
    NSData* data=[pipe.fileHandleForReading readDataToEndOfFile];
    return [NSString stringWithFormat:@"exit=%d\n%@",task.terminationStatus,
        Redact([[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding] ?: @"")];
}
static NSString* BinaryUUID(NSString* path) {
    NSFileHandle* file=[NSFileHandle fileHandleForReadingAtPath:path];
    NSData* data=nil;
    @try { data=[file readDataOfLength:65536]; }
    @catch (NSException*) { return @"unavailable"; }
    @finally { [file closeFile]; }
    if (data.length<sizeof(mach_header_64)) return @"unavailable";
    const auto* header=static_cast<const mach_header_64*>(data.bytes);
    if (header->magic!=MH_MAGIC_64) return @"unknown architecture";
    NSUInteger offset=sizeof(*header);
    for (uint32_t i=0;i<header->ncmds;i++) {
        if (offset+sizeof(load_command)>data.length) break;
        const auto* command=reinterpret_cast<const load_command*>(static_cast<const char*>(data.bytes)+offset);
        if (command->cmdsize<sizeof(load_command) || command->cmdsize>data.length-offset) break;
        if (command->cmd==LC_UUID && command->cmdsize>=sizeof(uuid_command)) {
            const auto* uuid=reinterpret_cast<const uuid_command*>(command);
            return [[NSUUID alloc] initWithUUIDBytes:uuid->uuid].UUIDString;
        }
        offset+=command->cmdsize;
    }
    return @"unavailable";
}
static NSString* Report(NSString* game, NSString* config) {
    NSMutableString* out=[NSMutableString stringWithString:
        @"DKR-R Mac diagnostics\nReview before sharing. Nothing is uploaded.\nROMs, saves, settings files, account/device identifiers and full Apple crash reports are excluded.\n\n"];
    NSDictionary* info=[NSDictionary dictionaryWithContentsOfFile:Join(game,@"Contents/Info.plist")];
    char machine[128]={}; size_t size=sizeof(machine); sysctlbyname("hw.model",machine,&size,nullptr,0);
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    [out appendFormat:@"macOS: %@\nHardware model: %s\nMemory: %llu GiB\nMetal device: %@\nRelease: %@\nMinimum macOS: %@\n",
        NSProcessInfo.processInfo.operatingSystemVersionString,machine,
        NSProcessInfo.processInfo.physicalMemory/(1024ULL*1024*1024),device.name ?: @"unavailable",
        info[@"CFBundleLongVersionString"] ?: @"unknown",info[@"LSMinimumSystemVersion"] ?: @"unknown"];
    NSString* executable=Join(game,@"Contents/MacOS/DKR-R");
    [out appendFormat:@"Game executable present: %@\nQuarantine attribute present: %@\nSignature verification: %@\nMach-O UUIDs: %@\n",
        [NSFileManager.defaultManager isExecutableFileAtPath:executable]?@"yes":@"no",
        getxattr(game.fileSystemRepresentation,"com.apple.quarantine",nullptr,0,0,0)>=0?@"yes":@"no",
        RunCheck(@"/usr/bin/codesign",@[@"--verify",@"--deep",@"--strict",game]),
        BinaryUUID(executable)];
    auto addLog = [&](NSString* label,NSString* file) {
        [out appendFormat:@"\n--- %@ ---\n%@",label,Redact(ReadTail(file))];
    };
    addLog(@"Current runtime log",Join(config,@"logs/runtime.log"));
    addLog(@"Previous runtime log",Join(config,@"logs/runtime-previous.log"));
    NSUInteger i=0;
    for (NSString* path in Recent(Join(config,@"logs/history"),@"runtime-",[NSSet setWithObject:@"log"],8))
        addLog([NSString stringWithFormat:@"Archived runtime log %lu",++i],path);
    i=0;
    for (NSString* path in Recent(Join(config,@"crash-dumps"),@"crash-",[NSSet setWithObject:@"log"],8))
        addLog([NSString stringWithFormat:@"Fatal signal record %lu",++i],path);
    NSString* apple=Join(NSHomeDirectory(),@"Library/Logs/DiagnosticReports"); i=0;
    for (NSString* path in Recent(apple,@"DKR-R",[NSSet setWithArray:@[@"ips",@"crash"]],3))
        [out appendFormat:@"\n--- Apple crash summary %lu ---\n%@",++i,AppleCrashSummary(path)];
    if (!i) [out appendString:@"\nNo readable Apple DKR-R crash report was found.\n"];
    i=0;
    for (NSString* path in Recent(DiagnosticRoot(),@"launch-",[NSSet setWithObject:@"log"],3))
        addLog([NSString stringWithFormat:@"Diagnostic launch output %lu",++i],path);
    // A fresh-settings run writes runtime logs separately from the user's profile.
    NSString* last=Join(DiagnosticRoot(),@"last-fresh-config.txt");
    NSString* fresh=[[NSString stringWithContentsOfFile:last encoding:NSUTF8StringEncoding error:nil]
        stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    if ([fresh hasPrefix:[DiagnosticRoot() stringByAppendingString:@"/fresh-"]]) {
        addLog(@"Fresh-settings runtime log",Join(fresh,@"logs/runtime.log"));
        for (NSString* path in Recent(Join(fresh,@"crash-dumps"),@"crash-",[NSSet setWithObject:@"log"],3))
            addLog(@"Fresh-settings fatal signal record",path);
    }
    return out;
}

@interface Diagnostics : NSObject <NSApplicationDelegate>
@property NSWindow* window;
@property NSTextField* status;
@property NSString* game;
@property NSString* config;
@property NSTask* child;
@end
@implementation Diagnostics
- (void)applicationDidFinishLaunching:(NSNotification*)notification {
    NSMenu* menu=[NSMenu new]; NSMenuItem* root=[NSMenuItem new]; [menu addItem:root];
    NSMenu* appMenu=[NSMenu new]; [appMenu addItemWithTitle:@"Quit Diagnostics" action:@selector(terminate:) keyEquivalent:@"q"];
    root.submenu=appMenu; NSApp.mainMenu=menu;
    self.window=[[NSWindow alloc] initWithContentRect:NSMakeRect(0,0,590,370)
        styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:NO];
    self.window.title=@"DKR-R Diagnostics"; self.window.releasedWhenClosed=NO;
    NSTextField* title=[NSTextField labelWithString:@"Having trouble opening DKR-R?"];
    title.font=[NSFont boldSystemFontOfSize:21]; title.frame=NSMakeRect(24,314,540,30); [self.window.contentView addSubview:title];
    NSTextField* detail=[NSTextField wrappingLabelWithString:
        @"Save recent logs and a system summary even if the game will not open. A fresh-settings launch uses a separate profile and leaves your normal saves and settings intact. Nothing is uploaded."];
    detail.frame=NSMakeRect(24,234,540,70); [self.window.contentView addSubview:detail];
    NSArray* titles=@[@"Save Diagnostic Report…",@"Launch with Logging",@"Launch with Fresh Settings",@"Choose DKR-R.app…"];
    SEL actions[]={@selector(save:),@selector(launch:),@selector(fresh:),@selector(choose:)};
    for (NSUInteger i=0;i<titles.count;i++) {
        NSButton* button=[NSButton buttonWithTitle:titles[i] target:self action:actions[i]];
        button.frame=NSMakeRect(24+(i%2)*276,180-(i/2)*48,266,36); [self.window.contentView addSubview:button];
    }
    self.status=[NSTextField wrappingLabelWithString:@"Choose the game if it was moved. Save a report before and after reproducing the problem."];
    self.status.frame=NSMakeRect(24,25,540,85); [self.window.contentView addSubview:self.status];
    [self.window center]; [self.window makeKeyAndOrderFront:nil]; [NSApp activateIgnoringOtherApps:YES];
}
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender { return YES; }
- (void)choose:(id)sender {
    NSOpenPanel* panel=[NSOpenPanel openPanel]; panel.allowedContentTypes=@[UTTypeApplicationBundle]; panel.canChooseDirectories=NO;
    panel.message=@"Select the DKR-R game app you want to diagnose.";
    if ([panel runModal]==NSModalResponseOK) {
        NSString* path=panel.URL.path;
        if ([NSFileManager.defaultManager isExecutableFileAtPath:Join(path,@"Contents/MacOS/DKR-R")]) {
            self.game=path; self.status.stringValue=@"Game selected. You can now save a report or try a diagnostic launch.";
        } else self.status.stringValue=@"That application does not contain DKR-R. Choose the game app.";
    }
}
- (void)save:(id)sender {
    NSSavePanel* panel=[NSSavePanel savePanel]; panel.allowedContentTypes=@[UTTypePlainText];
    panel.nameFieldStringValue=@"DKR-R-Diagnostics.txt";
    panel.message=@"Save a report to review and share with support. ROMs and saves are excluded.";
    if ([panel runModal]!=NSModalResponseOK) return;
    NSURL* url=panel.URL; self.status.stringValue=@"Collecting diagnostics…";
    NSString* game=self.game; NSString* config=self.config;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED,0), ^{
        NSString* report=Report(game,config); NSError* error=nil;
        BOOL ok=[report writeToURL:url atomically:YES encoding:NSUTF8StringEncoding error:&error];
        dispatch_async(dispatch_get_main_queue(), ^{
            self.status.stringValue=ok?@"Report saved. Review it before sharing it with support.":@"The report could not be saved. Choose another writable location.";
            if(ok) [NSWorkspace.sharedWorkspace activateFileViewerSelectingURLs:@[url]];
        });
    });
}
- (void)launch:(id)sender { [self startFresh:NO]; }
- (void)fresh:(id)sender { [self startFresh:YES]; }
- (void)startFresh:(BOOL)fresh {
    if (self.child.running) { self.status.stringValue=@"Close the diagnostic game session before starting another."; return; }
    for (NSRunningApplication* app in [NSRunningApplication runningApplicationsWithBundleIdentifier:@"io.github.thatguymcd.dkr-r"])
        if (!app.terminated) { self.status.stringValue=@"Quit DKR-R first so its logs and saves are not used by two sessions."; return; }
    NSFileManager* fm=NSFileManager.defaultManager;
    NSString* root=DiagnosticRoot(); NSError* error=nil;
    if (![fm createDirectoryAtPath:root withIntermediateDirectories:YES attributes:@{NSFilePosixPermissions:@0700} error:&error]) {
        self.status.stringValue=@"Cannot create diagnostic logs in Application Support."; return;
    }
    NSString* stamp=NSUUID.UUID.UUIDString;
    NSString* log=Join(root,[@"launch-" stringByAppendingFormat:@"%@.log",stamp]);
    [fm createFileAtPath:log contents:nil attributes:@{NSFilePosixPermissions:@0600}];
    NSFileHandle* handle=[NSFileHandle fileHandleForWritingAtPath:log];
    if (!handle) { self.status.stringValue=@"Cannot create a diagnostic launch log."; return; }
    NSString* config=self.config;
    if (fresh) {
        config=Join(root,[@"fresh-" stringByAppendingString:stamp]);
        [config writeToFile:Join(root,@"last-fresh-config.txt") atomically:YES encoding:NSUTF8StringEncoding error:nil];
    }
    NSTask* task=[NSTask new]; task.executableURL=[NSURL fileURLWithPath:Join(self.game,@"Contents/MacOS/DKR-R")];
    task.arguments=@[@"--config",config]; task.currentDirectoryURL=[NSURL fileURLWithPath:root];
    NSMutableDictionary* env=[NSProcessInfo.processInfo.environment mutableCopy]; env[@"DKR_DIAGNOSTIC_LOGGING"]=@"1";
    task.environment=env; task.standardOutput=handle; task.standardError=handle; task.standardInput=NSFileHandle.fileHandleWithNullDevice;
    task.terminationHandler=^(NSTask* ended) {
        NSString* end=[NSString stringWithFormat:@"\nDiagnostic launch ended: %@ %d\n",
            ended.terminationReason==NSTaskTerminationReasonUncaughtSignal?@"signal":@"exit",ended.terminationStatus];
        [handle writeData:[end dataUsingEncoding:NSUTF8StringEncoding]]; [handle closeFile];
        dispatch_async(dispatch_get_main_queue(), ^{ self.status.stringValue=[end stringByAppendingString:@"Save a diagnostic report now to include this attempt."]; });
    };
    if (![task launchAndReturnError:&error]) {
        [handle writeData:[error.localizedDescription dataUsingEncoding:NSUTF8StringEncoding]]; [handle closeFile];
        self.status.stringValue=@"The game could not launch. Save a report to include the launch error."; return;
    }
    self.child=task;
    self.status.stringValue=fresh?@"Started with fresh settings. Select your ROM, reproduce the problem, then quit the game and save a report.":@"Logging is enabled for this launch. Reproduce the problem, quit the game, then save a report.";
}
@end

int main(int argc, const char** argv) {
    @autoreleasepool {
        NSString* selfPath=NSBundle.mainBundle.bundlePath;
        NSString* parent=selfPath.stringByDeletingLastPathComponent;
        NSString* game=[parent.lastPathComponent isEqual:@"Helpers"] ? parent.stringByDeletingLastPathComponent.stringByDeletingLastPathComponent : Join(parent,@"DKR-R.app");
        if (![NSFileManager.defaultManager fileExistsAtPath:game]) game=@"/Applications/DKR-R.app";
        NSString* config=DefaultConfig(); NSString* output=nil;
        for (int i=1;i<argc;i++) {
            NSString* arg=@(argv[i]);
            if (([arg isEqual:@"--game"] || [arg isEqual:@"--config"] || [arg isEqual:@"--report"]) && i+1<argc) {
                NSString* value=@(argv[++i]);
                if ([arg isEqual:@"--game"]) game=value;
                else if ([arg isEqual:@"--config"]) config=value;
                else output=value;
            } else if ([arg isEqual:@"--self-test"]) {
                NSString* scrubbed=Redact(@"signal=0xb\nfile /Users/private-person/My ROM.z64\npassword=abc\npeer 192.168.0.1\nfriend user@example.com\n[boot] renderer failed\n");
                if ([scrubbed containsString:@"private-person"] || [scrubbed containsString:@"abc"] ||
                    [scrubbed containsString:@"192.168"] || [scrubbed containsString:@"example.com"] ||
                    ![scrubbed containsString:@"renderer failed"] || ![scrubbed containsString:@"signal=0xb"]) return 1;
                NSString* fixture=Join(NSTemporaryDirectory(),[NSUUID.UUID.UUIDString stringByAppendingString:@".ips"]);
                NSDictionary* body=@{@"userID":@501,@"procPath":@"/Users/secret/Game",@"exception":@{@"type":@"EXC_BAD_ACCESS",@"signal":@"SIGSEGV"},
                    @"usedImages":@[@{@"name":@"DKR-R",@"uuid":@"test-binary-uuid",@"path":@"/Users/secret/Game"}],
                    @"threads":@[@{@"triggered":@YES,@"name":@"personal-name",@"frames":@[@{@"imageIndex":@0,@"imageOffset":@42,@"symbol":@"dkr::RenderFrame"},@{@"imageIndex":NSNull.null}]}]};
                NSMutableData* bytes=[[ @"{}\n" dataUsingEncoding:NSUTF8StringEncoding] mutableCopy];
                [bytes appendData:[NSJSONSerialization dataWithJSONObject:body options:0 error:nil]];
                [bytes writeToFile:fixture atomically:YES];
                NSString* summary=AppleCrashSummary(fixture); [NSFileManager.defaultManager removeItemAtPath:fixture error:nil];
                if (![summary containsString:@"EXC_BAD_ACCESS"] || ![summary containsString:@"dkr::RenderFrame"] ||
                    [summary containsString:@"secret"] || [summary containsString:@"personal-name"] || [summary containsString:@"501"]) return 1;
                puts("PASS diagnostics redaction, Apple crash field allowlist and malformed frame handling"); return 0;
            } else { fprintf(stderr,"Unknown or incomplete option\n"); return 2; }
        }
        if (output) {
            NSError* error=nil;
            return [Report(game,config) writeToFile:output atomically:YES encoding:NSUTF8StringEncoding error:&error]?0:1;
        }
        [NSApplication sharedApplication]; [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        Diagnostics* delegate=[Diagnostics new]; delegate.game=game; delegate.config=config;
        NSApp.delegate=delegate; [NSApp run];
    }
    return 0;
}
