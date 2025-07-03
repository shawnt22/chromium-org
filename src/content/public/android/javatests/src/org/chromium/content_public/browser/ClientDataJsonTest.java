// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.content_public.browser;

import static org.hamcrest.core.IsNot.not;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;

import static org.chromium.base.test.util.Matchers.containsString;

import android.util.JsonReader;
import android.util.JsonToken;

import androidx.test.filters.SmallTest;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.base.test.BaseJUnit4ClassRunner;
import org.chromium.base.test.util.Batch;
import org.chromium.blink.mojom.PaymentCredentialInstrument;
import org.chromium.blink.mojom.PaymentOptions;
import org.chromium.blink.mojom.ShownPaymentEntityLogo;
import org.chromium.content_public.browser.test.NativeLibraryTestUtils;
import org.chromium.payments.mojom.PaymentCurrencyAmount;
import org.chromium.url.GURL;
import org.chromium.url.Origin;
import org.chromium.url.mojom.Url;

import java.io.IOException;
import java.io.StringReader;

/** Unit tests for ClientDataJson */
@RunWith(BaseJUnit4ClassRunner.class)
@Batch(Batch.UNIT_TESTS)
public class ClientDataJsonTest {

    private static final String RELYING_PARTY_ID = "subdomain.example.test";
    private static final String ORIGIN = "https://example.test";
    private static final Origin TOP_ORIGIN =
            Origin.create(new GURL("https://www.chromium.test/pay"));
    // 3 bytes of 0 encode to "AAAA" in base64.
    private static final byte[] CHALLENGE_BYTES = new byte[3];

    @Before
    public void setUp() {
        NativeLibraryTestUtils.loadNativeLibraryNoBrowserProcess();
    }

    @Test
    @SmallTest
    public void testBuildClientDataJson() {
        PaymentOptions payment = createSamplePaymentOptions();
        String output =
                ClientDataJson.buildClientDataJson(
                        ClientDataRequestType.PAYMENT_GET,
                        ORIGIN,
                        CHALLENGE_BYTES,
                        /* isCrossOrigin= */ false,
                        payment,
                        RELYING_PARTY_ID,
                        TOP_ORIGIN);

        assertParsesJson(output);
        // Test that the output has the expected fields.
        assertThat(output, containsString("\"type\":\"payment.get\""));
        assertThat(output, containsString("\"challenge\":\"AAAA\""));
        assertThat(output, containsString(String.format("\"origin\":\"%s\"", ORIGIN)));
        assertThat(output, containsString("\"crossOrigin\":false"));
        assertThat(output, containsString(String.format("\"rpId\":\"%s\"", RELYING_PARTY_ID)));
        // The topOrigin is formatted with no trailing slash.
        assertThat(output, containsString("\"topOrigin\":\"https://www.chromium.test\""));
        assertThat(output, containsString("\"payeeOrigin\":\"https://test.example\""));
        assertThat(output, not(containsString("paymentEntitiesLogos")));
        assertThat(output, containsString(String.format("\"value\":\"%s\"", payment.total.value)));
        assertThat(
                output,
                containsString(String.format("\"currency\":\"%s\"", payment.total.currency)));
        assertThat(
                output,
                containsString(String.format("\"icon\":\"%s\"", payment.instrument.icon.url)));
        assertThat(
                output,
                containsString(
                        String.format("\"displayName\":\"%s\"", payment.instrument.displayName)));
        assertThat(
                output,
                containsString(String.format("\"details\":\"%s\"", payment.instrument.details)));
        assertThat(output, containsString(String.format("\"browserBoundPublicKey\":\"AQIDBA\"")));
    }

    @Test
    @SmallTest
    public void testBuildClientDataForJsonPaymentCredentialCreation() {
        PaymentOptions payment = createSamplePaymentOptions();
        String output =
                ClientDataJson.buildClientDataJson(
                        ClientDataRequestType.WEB_AUTHN_CREATE,
                        ORIGIN,
                        CHALLENGE_BYTES,
                        /* isCrossOrigin= */ false,
                        payment,
                        RELYING_PARTY_ID,
                        TOP_ORIGIN);

        assertParsesJson(output);
        assertThat(output, containsString("\"type\":\"webauthn.create\""));
        assertThat(output, containsString("\"challenge\":\"AAAA\""));
        assertThat(output, containsString(String.format("\"origin\":\"%s\"", ORIGIN)));
        assertThat(output, containsString("\"crossOrigin\":false"));
        assertThat(output, containsString("\"payment\":{\"browserBoundPublicKey\":\"AQIDBA\"}"));
    }

    @Test
    @SmallTest
    public void testBuildClientDataJsonWithEmptyDetails() {
        PaymentOptions payment = createSamplePaymentOptions();
        payment.instrument.details = "";
        String output =
                ClientDataJson.buildClientDataJson(
                        ClientDataRequestType.PAYMENT_GET,
                        ORIGIN,
                        CHALLENGE_BYTES,
                        /* isCrossOrigin= */ false,
                        payment,
                        RELYING_PARTY_ID,
                        TOP_ORIGIN);

        assertParsesJson(output);
        // Test that the output does not contain the details field.
        assertThat(output, not(containsString("\"details\":")));
    }

    @Test
    @SmallTest
    public void testBuildClientDataJsonWithZeroLogos() {
        PaymentOptions payment = createSamplePaymentOptions();
        payment.paymentEntitiesLogos = new ShownPaymentEntityLogo[] {};
        String output =
                ClientDataJson.buildClientDataJson(
                        ClientDataRequestType.PAYMENT_GET,
                        ORIGIN,
                        CHALLENGE_BYTES,
                        /* isCrossOrigin= */ true,
                        payment,
                        RELYING_PARTY_ID,
                        TOP_ORIGIN);

        assertParsesJson(output);
        assertThat(output, containsString("\"paymentEntitiesLogos\":[]"));
    }

    @Test
    @SmallTest
    public void testBuildClientDataJsonWithOneLogo() {
        PaymentOptions payment = createSamplePaymentOptions();
        payment.paymentEntitiesLogos =
                new ShownPaymentEntityLogo[] {
                    shownPaymentEntityLogo(
                            "https://www.example.test/logo_one.png", "logo_one_label")
                };
        String output =
                ClientDataJson.buildClientDataJson(
                        ClientDataRequestType.PAYMENT_GET,
                        ORIGIN,
                        CHALLENGE_BYTES,
                        /* isCrossOrigin= */ true,
                        payment,
                        RELYING_PARTY_ID,
                        TOP_ORIGIN);

        assertParsesJson(output);
        assertThat(
                output,
                containsString(
                        "\"paymentEntitiesLogos\":[{\"url\":\"https://www.example.test/logo_one.png\",\"label\":\"logo_one_label\"}]"));
    }

    @Test
    @SmallTest
    public void testBuildClientDataJsonWithTwoLogos() {
        PaymentOptions payment = createSamplePaymentOptions();
        payment.paymentEntitiesLogos =
                new ShownPaymentEntityLogo[] {
                    shownPaymentEntityLogo(
                            "https://www.example.test/logo_one.png", "logo_one_label"),
                    shownPaymentEntityLogo(
                            "https://www.example.test/logo_two.png", "logo_two_label"),
                };
        String output =
                ClientDataJson.buildClientDataJson(
                        ClientDataRequestType.PAYMENT_GET,
                        ORIGIN,
                        CHALLENGE_BYTES,
                        /* isCrossOrigin= */ true,
                        payment,
                        RELYING_PARTY_ID,
                        TOP_ORIGIN);

        assertParsesJson(output);
        assertThat(
                output,
                containsString(
                        "\"paymentEntitiesLogos\":[{\"url\":\"https://www.example.test/logo_one.png\",\"label\":\"logo_one_label\"},{\"url\":\"https://www.example.test/logo_two.png\",\"label\":\"logo_two_label\"}]"));
    }

    private void assertParsesJson(String serializedJson) {
        try {
            JsonReader jsonReader = new JsonReader(new StringReader(serializedJson));
            // skipValue() throws a subclass of IOException when the serializedJson cannot be
            // parsed.
            jsonReader.skipValue();
            assertEquals(
                    "There is more than one value in the serialized json: " + serializedJson,
                    JsonToken.END_DOCUMENT,
                    jsonReader.peek());
        } catch (IOException e) {
            throw new AssertionError("Could not parse the serialized json: " + serializedJson, e);
        }
    }

    private PaymentOptions createSamplePaymentOptions() {
        PaymentOptions payment = new PaymentOptions();
        payment.total = new PaymentCurrencyAmount();
        payment.total.currency = "USD";
        payment.total.value = "123";
        payment.instrument = new PaymentCredentialInstrument();
        payment.instrument.displayName = "TestPay";
        payment.instrument.icon = new Url();
        payment.instrument.icon.url = "https://www.example.test/icon.png";
        payment.instrument.details = "instrument details";
        payment.payeeOrigin = new org.chromium.url.internal.mojom.Origin();
        payment.payeeOrigin.scheme = "https";
        payment.payeeOrigin.host = "test.example";
        payment.payeeOrigin.port = 443;
        payment.browserBoundPublicKey = new byte[] {0x01, 0x02, 0x03, 0x04};
        return payment;
    }

    private static ShownPaymentEntityLogo shownPaymentEntityLogo(String urlString, String label) {
        ShownPaymentEntityLogo logo = new ShownPaymentEntityLogo();
        Url url = new Url();
        url.url = urlString;
        logo.url = url;
        logo.label = label;
        return logo;
    }
}
