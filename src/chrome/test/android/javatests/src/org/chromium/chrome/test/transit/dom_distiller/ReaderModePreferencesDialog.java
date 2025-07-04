// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.test.transit.dom_distiller;

import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import android.widget.Button;
import android.widget.SeekBar;
import android.widget.Spinner;

import androidx.test.espresso.action.GeneralClickAction;
import androidx.test.espresso.action.GeneralLocation;
import androidx.test.espresso.action.Press;
import androidx.test.espresso.action.Tap;
import androidx.test.platform.app.InstrumentationRegistry;

import org.chromium.base.test.transit.CarryOn;
import org.chromium.base.test.transit.Triggers;
import org.chromium.base.test.transit.TripBuilder;
import org.chromium.base.test.transit.ViewElement;
import org.chromium.chrome.browser.app.ChromeActivity;
import org.chromium.chrome.test.R;
import org.chromium.chrome.test.util.MenuUtils;

/**
 * The Reader Mode preferences dialog, from where font, font size and background color can be set.
 *
 * <p>TODO(crbug.com/350074837): Turn this into a Facility when CctPageStation exists.
 */
public class ReaderModePreferencesDialog extends CarryOn {
    public ViewElement<Button> darkButtonElement;
    public ViewElement<Button> sepiaButtonElement;
    public ViewElement<Button> lightButtonElement;
    public ViewElement<Spinner> fontFamilySpinnerElement;
    public ViewElement<SeekBar> fontSizeSliderElement;

    public ReaderModePreferencesDialog() {
        /*
        DecorView
        ╰── LinearLayout
            ╰── FrameLayout
                ╰── @id/action_bar_root | FitWindowsFrameLayout
                    ╰── @id/content | ContentFrameLayout
                        ╰── @id/parentPanel | AlertDialogLayout
                            ╰── @id/customPanel | FrameLayout
                                ╰── @id/custom | FrameLayout
                                    ╰── DistilledPagePrefsView
                                        ├── @id/radio_button_group | RadioGroup
                                        │   ├── "Light" | @id/light_mode | MaterialRadioButton
                                        │   ├── "Dark" | @id/dark_mode | MaterialRadioButton
                                        │   ╰── "Sepia" | @id/sepia_mode | MaterialRadioButton
                                        ├── @id/font_family | AppCompatSpinner
                                        │   ╰── "Sans Serif" | @id/text1 | MaterialTextView
                                        ╰── LinearLayout
                                            ├── "200%" | @id/font_size_percentage | MaterialTextView
                                            ├── "A" | MaterialTextView
                                            ├── @id/font_size | AppCompatSeekBar
                                            ╰── "A" | MaterialTextView
        */
        darkButtonElement = declareView(Button.class, withText("Dark"));
        sepiaButtonElement = declareView(Button.class, withText("Sepia"));
        lightButtonElement = declareView(Button.class, withText("Light"));
        fontFamilySpinnerElement = declareView(Spinner.class, withId(R.id.font_family));
        fontSizeSliderElement = declareView(SeekBar.class, withId(R.id.font_size));
    }

    public TripBuilder setFontSizeSliderToMinTo() {
        // Min is 50% font size.
        return fontSizeSliderElement.performViewActionTo(
                new GeneralClickAction(Tap.SINGLE, GeneralLocation.CENTER_LEFT, Press.FINGER));
    }

    public TripBuilder setFontSizeSliderToMaxTo() {
        return fontSizeSliderElement.performViewActionTo(
                new GeneralClickAction(Tap.SINGLE, GeneralLocation.CENTER_RIGHT, Press.FINGER));
    }

    public static ReaderModePreferencesDialog open(ChromeActivity activity) {
        // TODO(crbug.com/350074837): when this is a Facility, use
        // ChromeTriggers#invokeCustomMenuActionTo().
        return Triggers.runTo(
                        () ->
                                MenuUtils.invokeCustomMenuActionSync(
                                        InstrumentationRegistry.getInstrumentation(),
                                        activity,
                                        R.id.reader_mode_prefs_id))
                .pickUpCarryOn(new ReaderModePreferencesDialog());
    }
}
