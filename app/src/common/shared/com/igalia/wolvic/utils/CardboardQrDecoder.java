/* -*- Mode: Java; c-basic-offset: 4; tab-width: 4; indent-tabs-mode: nil; -*-
 * Google Cardboard Official DeviceParams Protobuf Decoder
 */

package com.igalia.wolvic.utils;

import android.util.Base64;
import android.util.Log;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

public class CardboardQrDecoder {
    private static final String LOGTAG = SystemUtils.createLogtag(CardboardQrDecoder.class);

    public static class CardboardProfile {
        public float screenToLensMeters = 0.039f; // Default 39mm (Field 3)
        public float interLensMeters = 0.064f;    // Default 64mm (Field 4 - IPD)
        public int verticalAlignment = 0;          // Default 0 = BOTTOM (Field 5)
        public float trayToLensCenterMeters = 0.035f; // Default 35mm (Field 6)
        public float k1 = 0.340f;                 // Radial distortion k1 (Field 7)
        public float k2 = 0.550f;                 // Radial distortion k2 (Field 7)
        public float fovLeft = 50.0f;             // Outer FOV (Field 8)
        public float fovRight = 50.0f;            // Inner FOV (Field 8)
        public float fovTop = 50.0f;              // Top FOV (Field 8)
        public float fovBottom = 50.0f;           // Bottom FOV (Field 8)
        public String vendor = "Cardboard";        // Field 1
        public String model = "VR Viewer";         // Field 2
        public boolean valid = false;
    }

    /**
     * Parses a Google Cardboard QR Code URL or Base64 protobuf string
     * extracting ALL official Cardboard parameters.
     */
    public static CardboardProfile parseCardboardUrl(String urlOrBase64) {
        CardboardProfile profile = new CardboardProfile();
        if (urlOrBase64 == null || urlOrBase64.trim().isEmpty()) {
            return profile;
        }

        String base64Payload = null;
        String rawStr = urlOrBase64.trim();

        if (rawStr.contains("p=")) {
            int pIndex = rawStr.indexOf("p=");
            base64Payload = rawStr.substring(pIndex + 2);
            int ampIndex = base64Payload.indexOf("&");
            if (ampIndex != -1) {
                base64Payload = base64Payload.substring(0, ampIndex);
            }
        } else {
            base64Payload = rawStr;
        }

        if (base64Payload == null || base64Payload.isEmpty()) {
            return profile;
        }

        try {
            String normalizedBase64 = base64Payload.replace('-', '+').replace('_', '/');
            while (normalizedBase64.length() % 4 != 0) {
                normalizedBase64 += "=";
            }

            byte[] data = Base64.decode(normalizedBase64, Base64.DEFAULT);

            // Protobuf tag parsing loop for Cardboard DeviceParams
            for (int i = 0; i < data.length - 1; i++) {
                int tag = data[i] & 0xFF;

                // Field 1: Vendor (String, tag 0x0A)
                if (tag == 0x0A && i + 1 < data.length) {
                    int len = data[i + 1] & 0xFF;
                    if (len > 0 && i + 2 + len <= data.length) {
                        profile.vendor = new String(data, i + 2, len);
                        i += 1 + len;
                        continue;
                    }
                }

                // Field 2: Model (String, tag 0x12)
                if (tag == 0x12 && i + 1 < data.length) {
                    int len = data[i + 1] & 0xFF;
                    if (len > 0 && i + 2 + len <= data.length) {
                        profile.model = new String(data, i + 2, len);
                        i += 1 + len;
                        continue;
                    }
                }

                // Field 3: screen_to_lens_distance (float32, tag 0x1D)
                if (tag == 0x1D && i + 4 < data.length) {
                    float dist = ByteBuffer.wrap(data, i + 1, 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
                    if (!Float.isNaN(dist) && dist >= 0.010f && dist <= 0.120f) {
                        profile.screenToLensMeters = dist;
                        profile.valid = true;
                    }
                    i += 4;
                    continue;
                }

                // Field 4: inter_lens_distance (float32, tag 0x25 - IPD)
                if (tag == 0x25 && i + 4 < data.length) {
                    float ipd = ByteBuffer.wrap(data, i + 1, 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
                    if (!Float.isNaN(ipd) && ipd >= 0.045f && ipd <= 0.085f) {
                        profile.interLensMeters = ipd;
                        profile.valid = true;
                    }
                    i += 4;
                    continue;
                }

                // Field 5: vertical_alignment (varint, tag 0x28)
                if (tag == 0x28 && i + 1 < data.length) {
                    int align = data[i + 1] & 0xFF;
                    if (align >= 0 && align <= 2) {
                        profile.verticalAlignment = align;
                    }
                    i += 1;
                    continue;
                }

                // Field 6: tray_to_lens_center_distance (float32, tag 0x35)
                if (tag == 0x35 && i + 4 < data.length) {
                    float trayDist = ByteBuffer.wrap(data, i + 1, 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
                    if (!Float.isNaN(trayDist) && trayDist >= 0.015f && trayDist <= 0.100f) {
                        profile.trayToLensCenterMeters = trayDist;
                    }
                    i += 4;
                    continue;
                }

                // Field 7: distortion_coefficients (repeated float32, tag 0x3D or 0x3A)
                if ((tag == 0x3D || tag == 0x3A) && i + 8 < data.length) {
                    int offset = (tag == 0x3A) ? 2 : 1;
                    float k1Val = ByteBuffer.wrap(data, i + offset, 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
                    float k2Val = ByteBuffer.wrap(data, i + offset + 4, 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
                    if (!Float.isNaN(k1Val) && !Float.isNaN(k2Val)) {
                        profile.k1 = k1Val;
                        profile.k2 = k2Val;
                    }
                    i += offset + 7;
                    continue;
                }

                // Field 8: left_eye_field_of_view_angles (repeated float32, tag 0x42 or 0x45)
                if ((tag == 0x42 || tag == 0x45) && i + 16 < data.length) {
                    int offset = (tag == 0x42) ? 2 : 1;
                    float f1 = ByteBuffer.wrap(data, i + offset, 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
                    float f2 = ByteBuffer.wrap(data, i + offset + 4, 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
                    float f3 = ByteBuffer.wrap(data, i + offset + 8, 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
                    float f4 = ByteBuffer.wrap(data, i + offset + 12, 4).order(ByteOrder.LITTLE_ENDIAN).getFloat();
                    if (!Float.isNaN(f1) && f1 >= 20.0f && f1 <= 90.0f) {
                        profile.fovLeft = f1;
                        profile.fovRight = f2;
                        profile.fovTop = f3;
                        profile.fovBottom = f4;
                    }
                    i += offset + 15;
                    continue;
                }
            }

            Log.d(LOGTAG, String.format("Decoded Official Cardboard QR Profile: '%s %s' | IPD=%.1fmm LensDist=%.1fmm VertAlign=%d TrayLensDist=%.1fmm k1=%.3f k2=%.3f FOV=[%.0f,%.0f,%.0f,%.0f]",
                    profile.vendor, profile.model, profile.interLensMeters * 1000.0f, profile.screenToLensMeters * 1000.0f, profile.verticalAlignment, profile.trayToLensCenterMeters * 1000.0f, profile.k1, profile.k2, profile.fovLeft, profile.fovRight, profile.fovTop, profile.fovBottom));

        } catch (Exception e) {
            Log.e(LOGTAG, "Error parsing Cardboard QR protobuf payload: " + e.getMessage());
        }

        return profile;
    }
}
