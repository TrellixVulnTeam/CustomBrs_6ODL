// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "chrome/browser/ui/cocoa/autofill/autofill_popup_view_cocoa.h"

#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "chrome/browser/ui/autofill/autofill_popup_controller.h"
#include "chrome/browser/ui/autofill/popup_constants.h"
#include "chrome/browser/ui/cocoa/autofill/autofill_popup_view_bridge.h"
#include "components/autofill/core/browser/popup_item_ids.h"
#include "grit/ui_resources.h"
#include "ui/base/cocoa/window_size_constants.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/point.h"
#include "ui/gfx/rect.h"

using autofill::AutofillPopupView;

@interface AutofillPopupViewCocoa ()

#pragma mark -
#pragma mark Private methods

// Draws an Autofill suggestion in the given |bounds|, labeled with the given
// |name| and |subtext| hint.  If the suggestion |isSelected|, then it is drawn
// with a highlight.  |index| determines the font to use, as well as the icon,
// if the row requires it -- such as for credit cards.
- (void)drawSuggestionWithName:(NSString*)name
                       subtext:(NSString*)subtext
                         index:(size_t)index
                        bounds:(NSRect)bounds
                      selected:(BOOL)isSelected;

// Returns the icon for the row with the given |index|, or |nil| if there is
// none.
- (NSImage*)iconAtIndex:(size_t)index;

@end

@implementation AutofillPopupViewCocoa

#pragma mark -
#pragma mark Initialisers

- (id)initWithFrame:(NSRect)frame {
  NOTREACHED();
  return [self initWithController:NULL frame:frame];
}

- (id)initWithController:(autofill::AutofillPopupController*)controller
                   frame:(NSRect)frame {
  self = [super initWithDelegate:controller frame:frame];
  if (self)
    controller_ = controller;

  return self;
}

#pragma mark -
#pragma mark NSView implementation:

- (void)drawRect:(NSRect)dirtyRect {
  // If the view is in the process of being destroyed, don't bother drawing.
  if (!controller_)
    return;

  [self drawBackgroundAndBorder];

  for (size_t i = 0; i < controller_->names().size(); ++i) {
    // Skip rows outside of the dirty rect.
    NSRect rowBounds =
        NSRectFromCGRect(controller_->GetRowBounds(i).ToCGRect());
    if (!NSIntersectsRect(rowBounds, dirtyRect))
      continue;

    if (controller_->identifiers()[i] == autofill::POPUP_ITEM_ID_SEPARATOR) {
      [self drawSeparatorWithBounds:rowBounds];
    } else {
      NSString* name = SysUTF16ToNSString(controller_->names()[i]);
      NSString* subtext = SysUTF16ToNSString(controller_->subtexts()[i]);
      BOOL isSelected = static_cast<int>(i) == controller_->selected_line();
      [self drawSuggestionWithName:name
                           subtext:subtext
                             index:i
                            bounds:rowBounds
                          selected:isSelected];
    }
  }
}

#pragma mark -
#pragma mark Public API:

- (void)controllerDestroyed {
  // Since the |controller_| either already has been destroyed or is about to
  // be, about the only thing we can safely do with it is to null it out.
  controller_ = NULL;
  [super delegateDestroyed];
}

- (void)invalidateRow:(size_t)row {
  NSRect dirty_rect =
      NSRectFromCGRect(controller_->GetRowBounds(row).ToCGRect());
  [self setNeedsDisplayInRect:dirty_rect];
}

#pragma mark -
#pragma mark Private API:

- (void)drawSuggestionWithName:(NSString*)name
                       subtext:(NSString*)subtext
                         index:(size_t)index
                        bounds:(NSRect)bounds
                      selected:(BOOL)isSelected {
  // If this row is selected, highlight it.
  if (isSelected) {
    [[self highlightColor] set];
    [NSBezierPath fillRect:bounds];
  }

  BOOL isRTL = controller_->IsRTL();

  NSColor* nameColor =
      controller_->IsWarning(index) ? [self warningColor] : [self nameColor];
  NSDictionary* nameAttributes =
      [NSDictionary dictionaryWithObjectsAndKeys:
           controller_->GetNameFontListForRow(index).GetPrimaryFont().
               GetNativeFont(),
           NSFontAttributeName, nameColor, NSForegroundColorAttributeName,
           nil];
  NSSize nameSize = [name sizeWithAttributes:nameAttributes];
  CGFloat x = bounds.origin.x +
      (isRTL ?
       bounds.size.width - AutofillPopupView::kEndPadding - nameSize.width :
       AutofillPopupView::kEndPadding);
  CGFloat y = bounds.origin.y + (bounds.size.height - nameSize.height) / 2;

  [name drawAtPoint:NSMakePoint(x, y) withAttributes:nameAttributes];

  // The x-coordinate will be updated as each element is drawn.
  x = bounds.origin.x +
      (isRTL ?
       AutofillPopupView::kEndPadding :
       bounds.size.width - AutofillPopupView::kEndPadding);

  // Draw the Autofill icon, if one exists.
  NSImage* icon = [self iconAtIndex:index];
  if (icon) {
    NSSize iconSize = [icon size];
    x += isRTL ? 0 : -iconSize.width;
    y = bounds.origin.y + (bounds.size.height - iconSize.height) / 2;
    [icon drawInRect:NSMakeRect(x, y, iconSize.width, iconSize.height)
            fromRect:NSZeroRect
           operation:NSCompositeSourceOver
            fraction:1.0
      respectFlipped:YES
               hints:nil];

    x += isRTL ?
        iconSize.width + AutofillPopupView::kIconPadding :
        -AutofillPopupView::kIconPadding;
  }

  // Draw the subtext.
  NSDictionary* subtextAttributes =
      [NSDictionary dictionaryWithObjectsAndKeys:
           controller_->subtext_font_list().GetPrimaryFont().GetNativeFont(),
           NSFontAttributeName,
           [self subtextColor],
           NSForegroundColorAttributeName,
           nil];
  NSSize subtextSize = [subtext sizeWithAttributes:subtextAttributes];
  x += isRTL ? 0 : -subtextSize.width;
  y = bounds.origin.y + (bounds.size.height - subtextSize.height) / 2;

  [subtext drawAtPoint:NSMakePoint(x, y) withAttributes:subtextAttributes];
}

- (NSImage*)iconAtIndex:(size_t)index {
  if (controller_->icons()[index].empty())
    return nil;

  int iconId = controller_->GetIconResourceID(controller_->icons()[index]);
  DCHECK_NE(-1, iconId);

  ui::ResourceBundle& rb = ui::ResourceBundle::GetSharedInstance();
  return rb.GetNativeImageNamed(iconId).ToNSImage();
}

@end
