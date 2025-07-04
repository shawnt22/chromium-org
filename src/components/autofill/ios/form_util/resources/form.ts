// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @fileoverview Contains method needed to access the forms and their elements.
 */

import {RENDERER_ID_NOT_SET} from '//components/autofill/ios/form_util/resources/fill_constants.js';
import {getRemoteFrameToken} from '//components/autofill/ios/form_util/resources/fill_util.js';
import {gCrWeb, gCrWebLegacy} from '//ios/web/public/js_messaging/resources/gcrweb.js';
import {sendWebKitMessage, trim} from '//ios/web/public/js_messaging/resources/utils.js';

/**
 * Prefix used in references to form elements that have no 'id' or 'name'
 */
const kNamelessFormIDPrefix = 'gChrome~form~';

/**
 * Prefix used in references to field elements that have no 'id' or 'name' but
 * are included in a form.
 */
const kNamelessFieldIDPrefix = 'gChrome~field~';

/**
 * A WeakMap to track if the current value of a field was entered by user or
 * programmatically.
 * If the map is null, the source of changed is not track.
 */
const wasEditedByUser: WeakMap<any, any> = new WeakMap();

/**
 * Registry that tracks the forms that were submitted during the frame's
 * lifetime. Elements that are garbage collected will be removed from the
 * registry so this can't memory leak. In the worst case the registry will get
 * as big as the number of submitted forms that aren't yet deleted and we don't
 * expect a lot of those.
 */
const formSubmissionRegistry: WeakSet<any> = new WeakSet();

/**
 * Based on Element::isFormControlElement() (WebKit)
 * @param element A DOM element.
 * @return true if the `element` is a form control element.
 */
function isFormControlElement(element: Element): boolean {
  const tagName = element.tagName;
  return (
      tagName === 'INPUT' || tagName === 'SELECT' || tagName === 'TEXTAREA');
}

/**
 * Returns an array of control elements in a form.
 *
 * This method is based on the logic in method
 *     void WebFormElement::getFormControlElements(
 *         WebVector<WebFormControlElement>&) const
 * in chromium/src/third_party/WebKit/Source/WebKit/chromium/src/
 * WebFormElement.cpp.
 *
 * @param form A form element for which the control elements are returned.
 * @return An array of form control elements.
 */
function getFormControlElements(form: HTMLFormElement|null): Element[] {
  if (!form) {
    return [];
  }
  const results: Element[] = [];
  // Get input and select elements from form.elements.
  // According to
  // http://www.w3.org/TR/2011/WD-html5-20110525/forms.html, form.elements are
  // the "listed elements whose form owner is the form element, with the
  // exception of input elements whose type attribute is in the Image Button
  // state, which must, for historical reasons, be excluded from this
  // particular collection." In WebFormElement.cpp, this method is implemented
  // by returning elements in form's associated elements that have tag 'INPUT'
  // or 'SELECT'. Check if input Image Buttons are excluded in that
  // implementation. Note for Autofill, as input Image Button is not
  // considered as autofillable elements, there is no impact on Autofill
  // feature.
  for (const element of form.elements) {
    if (isFormControlElement(element)) {
      results.push(element);
    }
  }
  return results;
}

/**
 * Returns an array of iframe elements that are descendents of `root`.
 *
 * @param root The node under which to search for iframe elements.
 * @return An array of iframe elements.
 */
function getIframeElements(root: Element|null): HTMLIFrameElement[] {
  return Array.from(root?.querySelectorAll('iframe') ?? []) as
      HTMLIFrameElement[];
}

/**
 * Returns the field's `id` attribute if not space only; otherwise the
 * form's |name| attribute if the field is part of a form. Otherwise,
 * generate a technical identifier
 *
 * It is the identifier that should be used for the specified |element| when
 * storing Autofill data. This identifier will be used when filling the field
 * to lookup this field. The pair (getFormIdentifier, getFieldIdentifier) must
 * be unique on the page.
 * The following elements are considered to generate the identifier:
 * - the id of the element
 * - the name of the element if the element is part of a form
 * - the order of the element in the form if the element is part of the form.
 * - generate a xpath to the element and use it as an ID.
 *
 * Note: if this method returns '', the field will not be accessible and
 * cannot be autofilled.
 *
 * It aims to provide the logic in
 *     WebString nameForAutofill() const;
 * in chromium/src/third_party/WebKit/Source/WebKit/chromium/public/
 *  WebFormControlElement.h
 *
 * @param element An element of which the name for Autofill will be
 *     returned.
 * @return the name for Autofill.
 */
function getFieldIdentifier(element: Element|null): string {
  if (!element) {
    return '';
  }
  let trimmedIdentifier: string|null = element.id;
  if (trimmedIdentifier) {
    return trim(trimmedIdentifier);
  }
  if ('form' in element && element.form) {
    const form = element.form as HTMLFormElement;
    // The name of an element is only relevant as an identifier if the element
    // is part of a form.
    trimmedIdentifier = 'name' in element ? element.name as string : null;
    if (trimmedIdentifier) {
      trimmedIdentifier = trim(trimmedIdentifier);
      if (trimmedIdentifier!.length > 0) {
        return trimmedIdentifier!;
      }
    }

    const elements = getFormControlElements(form);
    for (let index = 0; index < elements.length; index++) {
      if (elements[index] === element) {
        return kNamelessFieldIDPrefix + index;
      }
    }
  }
  // Element is not part of a form and has no name or id, or usable attribute.
  // As best effort, try to find the closest ancestor with an id, then
  // check the index of the element in the descendants of the ancestors with
  // the same type.
  let ancestor: ParentNode|Element|null = element.parentNode;
  while (ancestor && ancestor instanceof Element &&
         (!ancestor.hasAttribute('id') || trim(ancestor.id) === '')) {
    ancestor = ancestor.parentNode;
  }

  let ancestorId = '';
  if (!ancestor || !(ancestor instanceof Element)) {
    ancestor = document.body;
  }
  if (ancestor instanceof Element && ancestor.hasAttribute('id')) {
    ancestorId = '#' + trim(ancestor.id);
  }
  const descendants = ancestor.querySelectorAll(element.tagName);
  let i = 0;
  for (i = 0; i < descendants.length; i++) {
    if (descendants[i] === element) {
      return kNamelessFieldIDPrefix + ancestorId + '~' + element.tagName + '~' +
          i;
    }
  }

  return '';
}

/**
 * Returns the field's `name` attribute if not space only; otherwise the
 * field's `id` attribute.
 *
 * The name will be used as a hint to infer the autofill type of the field.
 *
 * It aims to provide the logic in
 *     WebString nameForAutofill() const;
 * in chromium/src/third_party/WebKit/Source/WebKit/chromium/public/
 *  WebFormControlElement.h
 *
 * @param element An element of which the name for Autofill will be returned.
 * @return the name for Autofill.
 */
function getFieldName(element: Element|null): string {
  if (!element) {
    return '';
  }

  if ('name' in element && element.name) {
    const trimmedName = trim(element.name as string);
    if (trimmedName.length > 0) {
      return trimmedName;
    }
  }

  if (element.id) {
    return trim(element.id);
  }

  return '';
}

/**
 * Returns the form's `name` attribute if non-empty; otherwise the form's `id`
 * attribute, or the index of the form (with prefix) in document.forms.
 *
 * It is partially based on the logic in
 *     const string16 GetFormIdentifier(const blink::WebFormElement& form)
 * in chromium/src/components/autofill/renderer/form_autofill_util.h.
 *
 * @param form An element for which the identifier is returned.
 * @return a string that represents the element's identifier.
 */
function getFormIdentifier(form: Element|null): string {
  if (!form) {
    return '';
  }

  let name = form.getAttribute('name');
  if (name && name.length !== 0 &&
      form.ownerDocument.forms.namedItem(name) === form) {
    return name;
  }
  name = form.getAttribute('id');
  if (name && name.length !== 0 &&
      form.ownerDocument.getElementById(name) === form) {
    return name;
  }
  // A form name must be supplied, because the element will later need to be
  // identified from the name. A last resort is to take the index number of
  // the form in document.forms. ids are not supposed to begin with digits (by
  // HTML 4 spec) so this is unlikely to match a true id.
  for (let idx = 0; idx !== document.forms.length; idx++) {
    if (document.forms[idx] === form) {
      return kNamelessFormIDPrefix + idx;
    }
  }
  return '';
}

/**
 * Returns the form element from an ID obtained from getFormIdentifier.
 *
 * This works on a 'best effort' basis since DOM changes can always change the
 * actual element that the ID refers to.
 *
 * @param name An ID string obtained via getFormIdentifier.
 * @return The original form element, if it can be determined.
 */
function getFormElementFromIdentifier(name: string): HTMLFormElement|null {
  // First attempt is from the name / id supplied.
  const form = document.forms.namedItem(name);
  if (form) {
    return form.nodeType === Node.ELEMENT_NODE ? form : null;
  }
  // Second attempt is from the prefixed index position of the form in
  // document.forms.
  if (name.indexOf(kNamelessFormIDPrefix) === 0) {
    const nameAsInteger =
        0 | name.substring(kNamelessFieldIDPrefix.length).length;
    if (kNamelessFormIDPrefix + nameAsInteger === name &&
        nameAsInteger < document.forms.length) {
      const form = document.forms[nameAsInteger];
      return form ? form : null;
    }
  }
  return null;
}

/**
 * Returns the form element from an form renderer id.
 *
 * @param identifier An ID string obtained via getFormIdentifier.
 * @return The original form element, if it can be determined.
 */
function getFormElementFromRendererId(identifier: number): HTMLFormElement|
    null {
  if (identifier.toString() === RENDERER_ID_NOT_SET) {
    return null;
  }
  for (const form of document.forms) {
    if (identifier.toString() === gCrWebLegacy.fill.getUniqueID(form)) {
      return form;
    }
  }
  return null;
}

/**
 * Returns whether the last `input` or `change` event on `element` was
 * triggered by a user action (was "trusted"). Returns true by default if the
 * feature to fix the user edited bit isn't enabled which is the status quo.
 * TODO(crbug.com/40941928): Match Blink's behavior so that only a 'reset' event
 * makes an edited field unedited.
 */
function fieldWasEditedByUser(element: Element) {
  return !gCrWebLegacy.autofill_form_features
              .isAutofillCorrectUserEditedBitInParsedField() ||
      (wasEditedByUser.get(element) ?? false);
}

/**
 * @param originalURL A string containing a URL (absolute, relative...)
 * @return A string containing a full URL (absolute with scheme)
 */
function getFullyQualifiedUrl(originalURL: string): string {
  // A dummy anchor (never added to the document) is used to obtain the
  // fully-qualified URL of `originalURL`.
  const anchor = document.createElement('a');
  anchor.href = originalURL;
  return anchor.href;
}

// Send the form data to the browser.
function formSubmittedInternal(
    form: HTMLFormElement,
    messageHandler: string,
    programmaticSubmission: boolean,
    includeRemoteFrameToken: boolean = false,
    ): void {
  if (gCrWebLegacy.autofill_form_features
          .isAutofillDedupeFormSubmissionEnabled()) {
    // Handle deduping when the feature allows it.
    if (formSubmissionRegistry.has(form)) {
      // Do not double submit the same form.
      return;
    }
    formSubmissionRegistry.add(form);
  }

  // Default URL for action is the document's URL.
  const action = form.getAttribute('action') || document.URL;

  const message = {
    command: 'form.submit',
    frameID: gCrWeb.getFrameId(),
    formName: gCrWebLegacy.form.getFormIdentifier(form),
    href: getFullyQualifiedUrl(action),
    formData: gCrWebLegacy.fill.autofillSubmissionData(form),
    remoteFrameToken: includeRemoteFrameToken ? getRemoteFrameToken() :
                                                undefined,
    programmaticSubmission: programmaticSubmission,
  };

  sendWebKitMessage(messageHandler, message);
}

/**
 * Sends the form data to the browser. Errors that are caught via the try/catch
 * are reported to the browser. This is done before the error bubbles above
 * `formSubmitted()` so the generic JS errors wrapper doesn't intercept the
 * error before this custom error handler.
 *
 * @param form The form that was submitted.
 * @param messageHandler The name of the message handler to send the message to.
 * @param programmaticSubmission True if the form submission is programmatic.
 * @includeRemoteFrameToken True if the remote frame token should be included
 *   in the payload of the message sent to the browser.
 */
function formSubmitted(
    form: HTMLFormElement,
    messageHandler: string,
    programmaticSubmission: boolean,
    includeRemoteFrameToken: boolean = false,
    ): void {
  try {
    formSubmittedInternal(
        form, messageHandler, programmaticSubmission, includeRemoteFrameToken);
  } catch (error) {
    if (gCrWebLegacy.autofill_form_features
            .isAutofillReportFormSubmissionErrorsEnabled()) {
      reportFormSubmissionError(error, programmaticSubmission, messageHandler);
    } else {
      // Just let the error go through if not reported.
      throw error;
    }
  }
}

/**
 * Reports a form submission error to the browser.
 * @param error Object that holds information on the error.
 * @param programmaticSubmission True if the submission that errored was
 *   programmatic.
 * @param handler The name of the handler to send the error message to.
 */
function reportFormSubmissionError(
    error: any, programmaticSubmission: boolean, handler: string) {
  let errorMessage = '';
  let errorStack = '';
  if (error && error instanceof Error) {
    errorMessage = error.message;
    if (error.stack) {
      errorStack = error.stack;
    }
  }

  const message = {
    command: 'form.submit.error',
    errorStack,
    errorMessage,
    programmaticSubmission,
  };
  sendWebKitMessage(handler, message);
}

gCrWebLegacy.form = {
  wasEditedByUser,
  isFormControlElement,
  getFormControlElements,
  getIframeElements,
  getFieldIdentifier,
  getFieldName,
  getFormIdentifier,
  getFormElementFromIdentifier,
  getFormElementFromRendererId,
  fieldWasEditedByUser,
  formSubmitted,
  reportFormSubmissionError,
};
