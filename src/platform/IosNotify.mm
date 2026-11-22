// See IosNotify.h. Compiled on every Apple build (the src/*.mm glob); the body is iOS only.
#include "platform/IosNotify.h"
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#import <UserNotifications/UserNotifications.h>

// Lets a notification show while Deepwater is the app in front (iOS hides them otherwise).
@interface DWNotifyDelegate : NSObject <UNUserNotificationCenterDelegate>
@end
@implementation DWNotifyDelegate
- (void)userNotificationCenter:(UNUserNotificationCenter*)center
       willPresentNotification:(UNNotification*)notification
         withCompletionHandler:(void (^)(UNNotificationPresentationOptions))completionHandler {
  (void)center;
  (void)notification;
  completionHandler(UNNotificationPresentationOptionBanner | UNNotificationPresentationOptionList | UNNotificationPresentationOptionSound);
}
@end

namespace dw::ios {
void notify(const QString& title, const QString& text) {
  @autoreleasepool {
    UNUserNotificationCenter* c = [UNUserNotificationCenter currentNotificationCenter];
    static DWNotifyDelegate* delegate = nil;
    if (!delegate) {
      delegate = [[DWNotifyDelegate alloc] init];   // kept for the app's life (the center holds it weakly)
      c.delegate = delegate;
    }
    NSString* t = title.toNSString();
    NSString* b = text.toNSString();
    [c requestAuthorizationWithOptions:(UNAuthorizationOptionAlert | UNAuthorizationOptionSound)
                     completionHandler:^(BOOL granted, NSError* error) {
                       (void)error;
                       if (!granted) return;
                       UNMutableNotificationContent* content = [[UNMutableNotificationContent alloc] init];
                       content.title = t;
                       content.body = b;
                       content.sound = [UNNotificationSound defaultSound];
                       UNNotificationRequest* r = [UNNotificationRequest requestWithIdentifier:[[NSUUID UUID] UUIDString]
                                                                                       content:content
                                                                                       trigger:nil];
                       [[UNUserNotificationCenter currentNotificationCenter] addNotificationRequest:r withCompletionHandler:nil];
                     }];
  }
}
}  // namespace dw::ios
#else
namespace dw::ios {
void notify(const QString&, const QString&) {}
}
#endif
