/*****************************************************************************
 * progress_dlg.c
 *****************************************************************************
 * Copyright (C) 2012-2015 L-SMASH Works project
 *
 * Authors: rigaya <rigaya34589@live.jp>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license.
 * However, when distributing its binary file, it will be under LGPL or GPL.
 * Don't distribute it if its license is GPL. */

#include "progress_dlg.h"
#include "resource.h"
#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static BOOL CALLBACK dialog_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    switch (msg) {
    case WM_INITDIALOG:
        SetWindowLongPtr(hwnd, GWLP_USERDATA, lparam);
        break;
    case WM_COMMAND:
        if (wparam == IDCANCEL) {
            progress_dlg_t* dlg = (progress_dlg_t*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
            dlg->abort = TRUE;
        }
        break;
    default:
        break;
    }

    return FALSE;
}

void init_progress_dlg(progress_dlg_t* dlg, const char* module_name, int template_id)
{
    dlg->hnd = NULL;
    dlg->progress_percent = -1;
    dlg->abort = FALSE;
    dlg->hnd = CreateDialogParam(GetModuleHandle(module_name), MAKEINTRESOURCE(template_id), NULL, dialog_proc, (LPARAM)dlg);
}

void close_progress_dlg(progress_dlg_t* dlg)
{
    if (dlg->hnd)
        DestroyWindow(dlg->hnd);
    dlg->hnd = NULL;
    dlg->progress_percent = -1;
    dlg->abort = FALSE;
}

int update_progress_dlg(progress_dlg_t* dlg, const char* mes, int progress_percent)
{
    if (dlg->abort == FALSE) {
        for (MSG message; PeekMessage(&message, NULL, 0, 0, PM_REMOVE);) {
            TranslateMessage(&message);
            DispatchMessage(&message);
        }
        if (dlg->progress_percent < progress_percent) {
            dlg->progress_percent = progress_percent;
            progress_percent = min(progress_percent, 100);
            char window_mes[256];
            sprintf(window_mes, "%s... %d %%", mes, progress_percent);
            if (!IsWindowVisible(dlg->hnd))
                ShowWindow(dlg->hnd, SW_SHOW);
            HWND window_text = GetDlgItem(dlg->hnd, IDC_PERCENT_TEXT);
            SetWindowText(window_text, window_mes);
            HWND window_prg = GetDlgItem(dlg->hnd, IDC_PROGRESS);
            PostMessage(window_prg, PBM_SETPOS, progress_percent, 0);
        }
    }
    return dlg->abort;
}
