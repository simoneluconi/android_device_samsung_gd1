/*
 * SPDX-FileCopyrightText: 2026 The GalaxyCameraRevived Project
 * SPDX-License-Identifier: Apache-2.0
 */

package com.simoneluconi.gcnfc;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.widget.Toast;

/** A phone's NFC field reached the tag: NFC is not supported yet. */
public class FieldReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        Toast.makeText(context, R.string.nfc_not_supported, Toast.LENGTH_LONG).show();
    }
}
