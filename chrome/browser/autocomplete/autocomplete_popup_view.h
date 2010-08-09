// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file defines the interface class AutocompletePopupView.  Each toolkit
// will implement the popup view differently, so that code is inheriently
// platform specific.  However, the AutocompletePopupModel needs to do some
// communication with the view.  Since the model is shared between platforms,
// we need to define an interface that all view implementations will share.

#ifndef CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_VIEW_H_
#define CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_VIEW_H_
#pragma once

#include "build/build_config.h"

class AutocompletePopupModel;

class AutocompletePopupView {
 public:
  virtual ~AutocompletePopupView() {}

  // Returns true if the popup is currently open.
  virtual bool IsOpen() const = 0;

  // Invalidates one line of the autocomplete popup.
  virtual void InvalidateLine(size_t line) = 0;

  // Redraws the popup window to match any changes in the result set; this may
  // mean opening or closing the window.
  virtual void UpdatePopupAppearance() = 0;

  // Paint any pending updates.
  virtual void PaintUpdatesNow() = 0;

  // This method is called when the view should cancel any active drag (e.g.
  // because the user pressed ESC). The view may or may not need to take any
  // action (e.g. releasing mouse capture).  Note that this can be called when
  // no drag is in progress.
  virtual void OnDragCanceled() = 0;

  // Returns the popup's model.
  virtual AutocompletePopupModel* GetModel() = 0;

  // Returns the max y coordinate of the popup in screen coordinates.
  virtual int GetMaxYCoordinate() = 0;
};

#endif  // CHROME_BROWSER_AUTOCOMPLETE_AUTOCOMPLETE_POPUP_VIEW_H_
