// Copyright 2024 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unicode/uvernum.h>

#include "third_party/blink/renderer/platform/text/character.h"
#include "third_party/blink/renderer/platform/wtf/text/unicode.h"

namespace blink {

// Codepoints pair from unicode standardized variation sequences spec, compare:
// https://www.unicode.org/Public/UNIDATA/StandardizedVariants.txt
static const char kStandardizedVariationSequences[] =
    R"([[{\U00000030\U0000FE00}][{\U00002205\U0000FE00}][{\U00002229\U0000FE00}])"
    R"([{\U0000222A\U0000FE00}][{\U00002268\U0000FE00}][{\U00002269\U0000FE00}])"
    R"([{\U00002272\U0000FE00}][{\U00002273\U0000FE00}][{\U0000228A\U0000FE00}])"
    R"([{\U0000228B\U0000FE00}][{\U00002293\U0000FE00}][{\U00002294\U0000FE00}])"
    R"([{\U00002295\U0000FE00}][{\U00002297\U0000FE00}][{\U0000229C\U0000FE00}])"
    R"([{\U000022DA\U0000FE00}][{\U000022DB\U0000FE00}][{\U00002A3C\U0000FE00}])"
    R"([{\U00002A3D\U0000FE00}][{\U00002A9D\U0000FE00}][{\U00002A9E\U0000FE00}])"
    R"([{\U00002AAC\U0000FE00}][{\U00002AAD\U0000FE00}][{\U00002ACB\U0000FE00}])"
    R"([{\U00002ACC\U0000FE00}][{\U0000FF10\U0000FE00}][{\U00001D49C\U0000FE00}])"
    R"([{\U0000212C\U0000FE00}][{\U00001D49E\U0000FE00}][{\U00001D49F\U0000FE00}])"
    R"([{\U00002130\U0000FE00}][{\U00002131\U0000FE00}][{\U00001D4A2\U0000FE00}])"
    R"([{\U0000210B\U0000FE00}][{\U00002110\U0000FE00}][{\U00001D4A5\U0000FE00}])"
    R"([{\U00001D4A6\U0000FE00}][{\U00002112\U0000FE00}][{\U00002133\U0000FE00}])"
    R"([{\U00001D4A9\U0000FE00}][{\U00001D4AA\U0000FE00}][{\U00001D4AB\U0000FE00}])"
    R"([{\U00001D4AC\U0000FE00}][{\U0000211B\U0000FE00}][{\U00001D4AE\U0000FE00}])"
    R"([{\U00001D4AF\U0000FE00}][{\U00001D4B0\U0000FE00}][{\U00001D4B1\U0000FE00}])"
    R"([{\U00001D4B2\U0000FE00}][{\U00001D4B3\U0000FE00}][{\U00001D4B4\U0000FE00}])"
    R"([{\U00001D4B5\U0000FE00}][{\U00001D49C\U0000FE01}][{\U0000212C\U0000FE01}])"
    R"([{\U00001D49E\U0000FE01}][{\U00001D49F\U0000FE01}][{\U00002130\U0000FE01}])"
    R"([{\U00002131\U0000FE01}][{\U00001D4A2\U0000FE01}][{\U0000210B\U0000FE01}])"
    R"([{\U00002110\U0000FE01}][{\U00001D4A5\U0000FE01}][{\U00001D4A6\U0000FE01}])"
    R"([{\U00002112\U0000FE01}][{\U00002133\U0000FE01}][{\U00001D4A9\U0000FE01}])"
    R"([{\U00001D4AA\U0000FE01}][{\U00001D4AB\U0000FE01}][{\U00001D4AC\U0000FE01}])"
    R"([{\U0000211B\U0000FE01}][{\U00001D4AE\U0000FE01}][{\U00001D4AF\U0000FE01}])"
    R"([{\U00001D4B0\U0000FE01}][{\U00001D4B1\U0000FE01}][{\U00001D4B2\U0000FE01}])"
    R"([{\U00001D4B3\U0000FE01}][{\U00001D4B4\U0000FE01}][{\U00001D4B5\U0000FE01}])"
    R"([{\U00003001\U0000FE00}][{\U00003001\U0000FE01}][{\U00003002\U0000FE00}])"
    R"([{\U00003002\U0000FE01}][{\U0000FF01\U0000FE00}][{\U0000FF01\U0000FE01}])"
    R"([{\U0000FF0C\U0000FE00}][{\U0000FF0C\U0000FE01}][{\U0000FF0E\U0000FE00}])"
    R"([{\U0000FF0E\U0000FE01}][{\U0000FF1A\U0000FE00}][{\U0000FF1A\U0000FE01}])"
    R"([{\U0000FF1B\U0000FE00}][{\U0000FF1B\U0000FE01}][{\U0000FF1F\U0000FE00}])"
    R"([{\U0000FF1F\U0000FE01}][{\U00001000\U0000FE00}][{\U00001002\U0000FE00}])"
    R"([{\U00001004\U0000FE00}][{\U00001010\U0000FE00}][{\U00001011\U0000FE00}])"
    R"([{\U00001015\U0000FE00}][{\U00001019\U0000FE00}][{\U0000101A\U0000FE00}])"
    R"([{\U0000101C\U0000FE00}][{\U0000101D\U0000FE00}][{\U00001022\U0000FE00}])"
    R"([{\U00001031\U0000FE00}][{\U00001075\U0000FE00}][{\U00001078\U0000FE00}])"
    R"([{\U0000107A\U0000FE00}][{\U00001080\U0000FE00}][{\U0000AA60\U0000FE00}])"
    R"([{\U0000AA61\U0000FE00}][{\U0000AA62\U0000FE00}][{\U0000AA63\U0000FE00}])"
    R"([{\U0000AA64\U0000FE00}][{\U0000AA65\U0000FE00}][{\U0000AA66\U0000FE00}])"
    R"([{\U0000AA6B\U0000FE00}][{\U0000AA6C\U0000FE00}][{\U0000AA6F\U0000FE00}])"
    R"([{\U0000AA7A\U0000FE00}][{\U0000A856\U0000FE00}][{\U0000A85C\U0000FE00}])"
    R"([{\U0000A85E\U0000FE00}][{\U0000A85F\U0000FE00}][{\U0000A860\U0000FE00}])"
    R"([{\U0000A868\U0000FE00}][{\U000010AC5\U0000FE00}][{\U000010AC6\U0000FE00}])"
    R"([{\U000010AD6\U0000FE00}][{\U000010AD7\U0000FE00}][{\U000010AE1\U0000FE00}])"
    R"([{\U00001820\U0000180B}][{\U00001820\U0000180C}][{\U00001821\U0000180B}])"
    R"([{\U00001822\U0000180B}][{\U00001823\U0000180B}][{\U00001824\U0000180B}])"
    R"([{\U00001825\U0000180B}][{\U00001825\U0000180C}][{\U00001826\U0000180B}])"
    R"([{\U00001826\U0000180C}][{\U00001828\U0000180B}][{\U00001828\U0000180C}])"
    R"([{\U00001828\U0000180D}][{\U0000182A\U0000180B}][{\U0000182C\U0000180B}])"
    R"([{\U0000182C\U0000180C}][{\U0000182C\U0000180D}][{\U0000182D\U0000180B}])"
    R"([{\U0000182D\U0000180C}][{\U0000182D\U0000180D}][{\U00001830\U0000180B}])"
    R"([{\U00001830\U0000180C}][{\U00001832\U0000180B}][{\U00001833\U0000180B}])"
    R"([{\U00001835\U0000180B}][{\U00001836\U0000180B}][{\U00001836\U0000180C}])"
    R"([{\U00001838\U0000180B}][{\U00001844\U0000180B}][{\U00001845\U0000180B}])"
    R"([{\U00001846\U0000180B}][{\U00001847\U0000180B}][{\U00001847\U0000180C}])"
    R"([{\U00001848\U0000180B}][{\U00001849\U0000180B}][{\U0000184D\U0000180B}])"
    R"([{\U0000184E\U0000180B}][{\U0000185D\U0000180B}][{\U0000185E\U0000180B}])"
    R"([{\U0000185E\U0000180C}][{\U00001860\U0000180B}][{\U00001863\U0000180B}])"
    R"([{\U00001868\U0000180B}][{\U00001868\U0000180C}][{\U00001869\U0000180B}])"
    R"([{\U0000186F\U0000180B}][{\U00001873\U0000180B}][{\U00001873\U0000180C}])"
    R"([{\U00001873\U0000180D}][{\U00001874\U0000180B}][{\U00001874\U0000180C}])"
    R"([{\U00001874\U0000180D}][{\U00001876\U0000180B}][{\U00001880\U0000180B}])"
    R"([{\U00001881\U0000180B}][{\U00001887\U0000180B}][{\U00001887\U0000180C}])"
    R"([{\U00001887\U0000180D}][{\U00001888\U0000180B}][{\U0000188A\U0000180B}])"
    R"([{\U000013091\U0000FE00}][{\U000013092\U0000FE00}][{\U000013093\U0000FE01}])"
    R"([{\U0000130A9\U0000FE01}][{\U00001310F\U0000FE00}][{\U000013117\U0000FE02}])"
    R"([{\U00001311C\U0000FE00}][{\U000013121\U0000FE00}][{\U000013127\U0000FE00}])"
    R"([{\U000013139\U0000FE00}][{\U000013139\U0000FE02}][{\U000013183\U0000FE02}])"
    R"([{\U000013187\U0000FE01}][{\U0000131A0\U0000FE00}][{\U0000131A0\U0000FE02}])"
    R"([{\U0000131B1\U0000FE00}][{\U0000131B1\U0000FE01}][{\U0000131B8\U0000FE00}])"
    R"([{\U0000131B9\U0000FE00}][{\U0000131BA\U0000FE02}][{\U0000131CB\U0000FE00}])"
    R"([{\U0000131EE\U0000FE01}][{\U0000131EE\U0000FE02}][{\U0000131F8\U0000FE01}])"
    R"([{\U0000131F9\U0000FE00}][{\U0000131F9\U0000FE01}][{\U0000131FA\U0000FE00}])"
    R"([{\U0000131FA\U0000FE01}][{\U000013216\U0000FE02}][{\U000013257\U0000FE01}])"
    R"([{\U00001327B\U0000FE02}][{\U00001327F\U0000FE00}][{\U00001327F\U0000FE01}])"
    R"([{\U000013285\U0000FE00}][{\U00001328C\U0000FE00}][{\U0000132A4\U0000FE01}])"
    R"([{\U0000132A4\U0000FE02}][{\U0000132AA\U0000FE00}][{\U0000132CB\U0000FE00}])"
    R"([{\U0000132DC\U0000FE00}][{\U0000132E7\U0000FE00}][{\U0000132E7\U0000FE02}])"
    R"([{\U0000132E9\U0000FE02}][{\U0000132F8\U0000FE02}][{\U0000132FD\U0000FE02}])"
    R"([{\U000013302\U0000FE02}][{\U000013303\U0000FE02}][{\U000013307\U0000FE00}])"
    R"([{\U000013308\U0000FE01}][{\U000013310\U0000FE02}][{\U000013311\U0000FE02}])"
    R"([{\U000013312\U0000FE01}][{\U000013312\U0000FE02}][{\U000013313\U0000FE01}])"
    R"([{\U000013313\U0000FE02}][{\U000013314\U0000FE01}][{\U000013314\U0000FE02}])"
    R"([{\U00001331B\U0000FE00}][{\U00001331B\U0000FE01}][{\U00001331C\U0000FE02}])"
    R"([{\U000013321\U0000FE01}][{\U000013321\U0000FE02}][{\U000013322\U0000FE00}])"
    R"([{\U000013322\U0000FE01}][{\U000013331\U0000FE01}][{\U000013331\U0000FE02}])"
    R"([{\U00001333B\U0000FE00}][{\U00001333C\U0000FE00}][{\U00001334A\U0000FE02}])"
    R"([{\U000013361\U0000FE02}][{\U000013373\U0000FE02}][{\U000013377\U0000FE00}])"
    R"([{\U000013378\U0000FE00}][{\U00001337D\U0000FE02}][{\U000013385\U0000FE02}])"
    R"([{\U000013399\U0000FE00}][{\U00001339A\U0000FE00}][{\U0000133AF\U0000FE02}])"
    R"([{\U0000133B0\U0000FE02}][{\U0000133BF\U0000FE02}][{\U0000133D3\U0000FE00}])"
    R"([{\U0000133DD\U0000FE02}][{\U0000133F2\U0000FE00}][{\U0000133F5\U0000FE00}])"
    R"([{\U0000133F6\U0000FE00}][{\U000013403\U0000FE00}][{\U000013416\U0000FE00}])"
    R"([{\U000013419\U0000FE00}][{\U000013419\U0000FE01}][{\U000013419\U0000FE02}])"
    R"([{\U00001341A\U0000FE00}][{\U000013423\U0000FE00}][{\U00001342C\U0000FE02}])"
    R"([{\U00001342E\U0000FE02}][{\U000013443\U0000FE00}][{\U000013444\U0000FE00}])"
    R"([{\U000013445\U0000FE00}][{\U000013446\U0000FE00}][{\U0000349E\U0000FE00}])"
    R"([{\U000034B9\U0000FE00}][{\U000034BB\U0000FE00}][{\U000034DF\U0000FE00}])"
    R"([{\U00003515\U0000FE00}][{\U000036EE\U0000FE00}][{\U000036FC\U0000FE00}])"
    R"([{\U00003781\U0000FE00}][{\U0000382F\U0000FE00}][{\U00003862\U0000FE00}])"
    R"([{\U0000387C\U0000FE00}][{\U000038C7\U0000FE00}][{\U000038E3\U0000FE00}])"
    R"([{\U0000391C\U0000FE00}][{\U0000393A\U0000FE00}][{\U00003A2E\U0000FE00}])"
    R"([{\U00003A6C\U0000FE00}][{\U00003AE4\U0000FE00}][{\U00003B08\U0000FE00}])"
    R"([{\U00003B19\U0000FE00}][{\U00003B49\U0000FE00}][{\U00003B9D\U0000FE00}])"
    R"([{\U00003B9D\U0000FE01}][{\U00003C18\U0000FE00}][{\U00003C4E\U0000FE00}])"
    R"([{\U00003D33\U0000FE00}][{\U00003D96\U0000FE00}][{\U00003EAC\U0000FE00}])"
    R"([{\U00003EB8\U0000FE00}][{\U00003EB8\U0000FE01}][{\U00003F1B\U0000FE00}])"
    R"([{\U00003FFC\U0000FE00}][{\U00004008\U0000FE00}][{\U00004018\U0000FE00}])"
    R"([{\U00004039\U0000FE00}][{\U00004039\U0000FE01}][{\U00004046\U0000FE00}])"
    R"([{\U00004096\U0000FE00}][{\U000040E3\U0000FE00}][{\U0000412F\U0000FE00}])"
    R"([{\U00004202\U0000FE00}][{\U00004227\U0000FE00}][{\U000042A0\U0000FE00}])"
    R"([{\U00004301\U0000FE00}][{\U00004334\U0000FE00}][{\U00004359\U0000FE00}])"
    R"([{\U000043D5\U0000FE00}][{\U000043D9\U0000FE00}][{\U0000440B\U0000FE00}])"
    R"([{\U0000446B\U0000FE00}][{\U0000452B\U0000FE00}][{\U0000455D\U0000FE00}])"
    R"([{\U00004561\U0000FE00}][{\U0000456B\U0000FE00}][{\U000045D7\U0000FE00}])"
    R"([{\U000045F9\U0000FE00}][{\U00004635\U0000FE00}][{\U000046BE\U0000FE00}])"
    R"([{\U000046C7\U0000FE00}][{\U00004995\U0000FE00}][{\U000049E6\U0000FE00}])"
    R"([{\U00004A6E\U0000FE00}][{\U00004A76\U0000FE00}][{\U00004AB2\U0000FE00}])"
    R"([{\U00004B33\U0000FE00}][{\U00004BCE\U0000FE00}][{\U00004CCE\U0000FE00}])"
    R"([{\U00004CED\U0000FE00}][{\U00004CF8\U0000FE00}][{\U00004D56\U0000FE00}])"
    R"([{\U00004E0D\U0000FE00}][{\U00004E26\U0000FE00}][{\U00004E32\U0000FE00}])"
    R"([{\U00004E38\U0000FE00}][{\U00004E39\U0000FE00}][{\U00004E3D\U0000FE00}])"
    R"([{\U00004E41\U0000FE00}][{\U00004E82\U0000FE00}][{\U00004E86\U0000FE00}])"
    R"([{\U00004EAE\U0000FE00}][{\U00004EC0\U0000FE00}][{\U00004ECC\U0000FE00}])"
    R"([{\U00004EE4\U0000FE00}][{\U00004F60\U0000FE00}][{\U00004F80\U0000FE00}])"
    R"([{\U00004F86\U0000FE00}][{\U00004F8B\U0000FE00}][{\U00004FAE\U0000FE00}])"
    R"([{\U00004FAE\U0000FE01}][{\U00004FBB\U0000FE00}][{\U00004FBF\U0000FE00}])"
    R"([{\U00005002\U0000FE00}][{\U0000502B\U0000FE00}][{\U0000507A\U0000FE00}])"
    R"([{\U00005099\U0000FE00}][{\U000050CF\U0000FE00}][{\U000050DA\U0000FE00}])"
    R"([{\U000050E7\U0000FE00}][{\U000050E7\U0000FE01}][{\U00005140\U0000FE00}])"
    R"([{\U00005145\U0000FE00}][{\U0000514D\U0000FE00}][{\U0000514D\U0000FE01}])"
    R"([{\U00005154\U0000FE00}][{\U00005164\U0000FE00}][{\U00005167\U0000FE00}])"
    R"([{\U00005168\U0000FE00}][{\U00005169\U0000FE00}][{\U0000516D\U0000FE00}])"
    R"([{\U00005177\U0000FE00}][{\U00005180\U0000FE00}][{\U0000518D\U0000FE00}])"
    R"([{\U00005192\U0000FE00}][{\U00005195\U0000FE00}][{\U00005197\U0000FE00}])"
    R"([{\U000051A4\U0000FE00}][{\U000051AC\U0000FE00}][{\U000051B5\U0000FE00}])"
    R"([{\U000051B5\U0000FE01}][{\U000051B7\U0000FE00}][{\U000051C9\U0000FE00}])"
    R"([{\U000051CC\U0000FE00}][{\U000051DC\U0000FE00}][{\U000051DE\U0000FE00}])"
    R"([{\U000051F5\U0000FE00}][{\U00005203\U0000FE00}][{\U00005207\U0000FE00}])"
    R"([{\U00005207\U0000FE01}][{\U00005217\U0000FE00}][{\U00005229\U0000FE00}])"
    R"([{\U0000523A\U0000FE00}][{\U0000523B\U0000FE00}][{\U00005246\U0000FE00}])"
    R"([{\U00005272\U0000FE00}][{\U00005277\U0000FE00}][{\U00005289\U0000FE00}])"
    R"([{\U0000529B\U0000FE00}][{\U000052A3\U0000FE00}][{\U000052B3\U0000FE00}])"
    R"([{\U000052C7\U0000FE00}][{\U000052C7\U0000FE01}][{\U000052C9\U0000FE00}])"
    R"([{\U000052C9\U0000FE01}][{\U000052D2\U0000FE00}][{\U000052DE\U0000FE00}])"
    R"([{\U000052E4\U0000FE00}][{\U000052E4\U0000FE01}][{\U000052F5\U0000FE00}])"
    R"([{\U000052FA\U0000FE00}][{\U000052FA\U0000FE01}][{\U00005305\U0000FE00}])"
    R"([{\U00005306\U0000FE00}][{\U00005317\U0000FE00}][{\U00005317\U0000FE01}])"
    R"([{\U0000533F\U0000FE00}][{\U00005349\U0000FE00}][{\U00005351\U0000FE00}])"
    R"([{\U00005351\U0000FE01}][{\U0000535A\U0000FE00}][{\U00005373\U0000FE00}])"
    R"([{\U00005375\U0000FE00}][{\U0000537D\U0000FE00}][{\U0000537F\U0000FE00}])"
    R"([{\U0000537F\U0000FE01}][{\U0000537F\U0000FE02}][{\U000053C3\U0000FE00}])"
    R"([{\U000053CA\U0000FE00}][{\U000053DF\U0000FE00}][{\U000053E5\U0000FE00}])"
    R"([{\U000053EB\U0000FE00}][{\U000053F1\U0000FE00}][{\U00005406\U0000FE00}])"
    R"([{\U0000540F\U0000FE00}][{\U0000541D\U0000FE00}][{\U00005438\U0000FE00}])"
    R"([{\U00005442\U0000FE00}][{\U00005448\U0000FE00}][{\U00005468\U0000FE00}])"
    R"([{\U0000549E\U0000FE00}][{\U000054A2\U0000FE00}][{\U000054BD\U0000FE00}])"
    R"([{\U000054F6\U0000FE00}][{\U00005510\U0000FE00}][{\U00005553\U0000FE00}])"
    R"([{\U00005555\U0000FE00}][{\U00005563\U0000FE00}][{\U00005584\U0000FE00}])"
    R"([{\U00005584\U0000FE01}][{\U00005587\U0000FE00}][{\U00005599\U0000FE00}])"
    R"([{\U00005599\U0000FE01}][{\U0000559D\U0000FE00}][{\U0000559D\U0000FE01}])"
    R"([{\U000055AB\U0000FE00}][{\U000055B3\U0000FE00}][{\U000055C0\U0000FE00}])"
    R"([{\U000055C2\U0000FE00}][{\U000055E2\U0000FE00}][{\U00005606\U0000FE00}])"
    R"([{\U00005606\U0000FE01}][{\U00005651\U0000FE00}][{\U00005668\U0000FE00}])"
    R"([{\U00005674\U0000FE00}][{\U000056F9\U0000FE00}][{\U00005716\U0000FE00}])"
    R"([{\U00005717\U0000FE00}][{\U0000578B\U0000FE00}][{\U000057CE\U0000FE00}])"
    R"([{\U000057F4\U0000FE00}][{\U0000580D\U0000FE00}][{\U00005831\U0000FE00}])"
    R"([{\U00005832\U0000FE00}][{\U00005840\U0000FE00}][{\U0000585A\U0000FE00}])"
    R"([{\U0000585A\U0000FE01}][{\U0000585E\U0000FE00}][{\U000058A8\U0000FE00}])"
    R"([{\U000058AC\U0000FE00}][{\U000058B3\U0000FE00}][{\U000058D8\U0000FE00}])"
    R"([{\U000058DF\U0000FE00}][{\U000058EE\U0000FE00}][{\U000058F2\U0000FE00}])"
    R"([{\U000058F7\U0000FE00}][{\U00005906\U0000FE00}][{\U0000591A\U0000FE00}])"
    R"([{\U00005922\U0000FE00}][{\U00005944\U0000FE00}][{\U00005948\U0000FE00}])"
    R"([{\U00005951\U0000FE00}][{\U00005954\U0000FE00}][{\U00005962\U0000FE00}])"
    R"([{\U00005973\U0000FE00}][{\U000059D8\U0000FE00}][{\U000059EC\U0000FE00}])"
    R"([{\U00005A1B\U0000FE00}][{\U00005A27\U0000FE00}][{\U00005A62\U0000FE00}])"
    R"([{\U00005A66\U0000FE00}][{\U00005AB5\U0000FE00}][{\U00005B08\U0000FE00}])"
    R"([{\U00005B28\U0000FE00}][{\U00005B3E\U0000FE00}][{\U00005B3E\U0000FE01}])"
    R"([{\U00005B85\U0000FE00}][{\U00005BC3\U0000FE00}][{\U00005BD8\U0000FE00}])"
    R"([{\U00005BE7\U0000FE00}][{\U00005BE7\U0000FE01}][{\U00005BE7\U0000FE02}])"
    R"([{\U00005BEE\U0000FE00}][{\U00005BF3\U0000FE00}][{\U00005BFF\U0000FE00}])"
    R"([{\U00005C06\U0000FE00}][{\U00005C22\U0000FE00}][{\U00005C3F\U0000FE00}])"
    R"([{\U00005C60\U0000FE00}][{\U00005C62\U0000FE00}][{\U00005C64\U0000FE00}])"
    R"([{\U00005C65\U0000FE00}][{\U00005C6E\U0000FE00}][{\U00005C6E\U0000FE01}])"
    R"([{\U00005C8D\U0000FE00}][{\U00005CC0\U0000FE00}][{\U00005D19\U0000FE00}])"
    R"([{\U00005D43\U0000FE00}][{\U00005D50\U0000FE00}][{\U00005D6B\U0000FE00}])"
    R"([{\U00005D6E\U0000FE00}][{\U00005D7C\U0000FE00}][{\U00005DB2\U0000FE00}])"
    R"([{\U00005DBA\U0000FE00}][{\U00005DE1\U0000FE00}][{\U00005DE2\U0000FE00}])"
    R"([{\U00005DFD\U0000FE00}][{\U00005E28\U0000FE00}][{\U00005E3D\U0000FE00}])"
    R"([{\U00005E69\U0000FE00}][{\U00005E74\U0000FE00}][{\U00005EA6\U0000FE00}])"
    R"([{\U00005EB0\U0000FE00}][{\U00005EB3\U0000FE00}][{\U00005EB6\U0000FE00}])"
    R"([{\U00005EC9\U0000FE00}][{\U00005ECA\U0000FE00}][{\U00005ECA\U0000FE01}])"
    R"([{\U00005ED2\U0000FE00}][{\U00005ED3\U0000FE00}][{\U00005ED9\U0000FE00}])"
    R"([{\U00005EEC\U0000FE00}][{\U00005EFE\U0000FE00}][{\U00005F04\U0000FE00}])"
    R"([{\U00005F22\U0000FE00}][{\U00005F22\U0000FE01}][{\U00005F53\U0000FE00}])"
    R"([{\U00005F62\U0000FE00}][{\U00005F69\U0000FE00}][{\U00005F6B\U0000FE00}])"
    R"([{\U00005F8B\U0000FE00}][{\U00005F9A\U0000FE00}][{\U00005FA9\U0000FE00}])"
    R"([{\U00005FAD\U0000FE00}][{\U00005FCD\U0000FE00}][{\U00005FD7\U0000FE00}])"
    R"([{\U00005FF5\U0000FE00}][{\U00005FF9\U0000FE00}][{\U00006012\U0000FE00}])"
    R"([{\U0000601C\U0000FE00}][{\U00006075\U0000FE00}][{\U00006081\U0000FE00}])"
    R"([{\U00006094\U0000FE00}][{\U00006094\U0000FE01}][{\U000060C7\U0000FE00}])"
    R"([{\U000060D8\U0000FE00}][{\U000060E1\U0000FE00}][{\U00006108\U0000FE00}])"
    R"([{\U00006144\U0000FE00}][{\U00006148\U0000FE00}][{\U0000614C\U0000FE00}])"
    R"([{\U0000614C\U0000FE01}][{\U0000614E\U0000FE00}][{\U0000614E\U0000FE01}])"
    R"([{\U00006160\U0000FE00}][{\U00006168\U0000FE00}][{\U0000617A\U0000FE00}])"
    R"([{\U0000618E\U0000FE00}][{\U0000618E\U0000FE01}][{\U0000618E\U0000FE02}])"
    R"([{\U00006190\U0000FE00}][{\U000061A4\U0000FE00}][{\U000061AF\U0000FE00}])"
    R"([{\U000061B2\U0000FE00}][{\U000061DE\U0000FE00}][{\U000061F2\U0000FE00}])"
    R"([{\U000061F2\U0000FE01}][{\U000061F2\U0000FE02}][{\U000061F6\U0000FE00}])"
    R"([{\U000061F6\U0000FE01}][{\U00006200\U0000FE00}][{\U00006210\U0000FE00}])"
    R"([{\U0000621B\U0000FE00}][{\U0000622E\U0000FE00}][{\U00006234\U0000FE00}])"
    R"([{\U0000625D\U0000FE00}][{\U000062B1\U0000FE00}][{\U000062C9\U0000FE00}])"
    R"([{\U000062CF\U0000FE00}][{\U000062D3\U0000FE00}][{\U000062D4\U0000FE00}])"
    R"([{\U000062FC\U0000FE00}][{\U000062FE\U0000FE00}][{\U0000633D\U0000FE00}])"
    R"([{\U00006350\U0000FE00}][{\U00006368\U0000FE00}][{\U0000637B\U0000FE00}])"
    R"([{\U00006383\U0000FE00}][{\U000063A0\U0000FE00}][{\U000063A9\U0000FE00}])"
    R"([{\U000063C4\U0000FE00}][{\U000063C5\U0000FE00}][{\U000063E4\U0000FE00}])"
    R"([{\U0000641C\U0000FE00}][{\U00006422\U0000FE00}][{\U00006452\U0000FE00}])"
    R"([{\U00006469\U0000FE00}][{\U00006477\U0000FE00}][{\U0000647E\U0000FE00}])"
    R"([{\U0000649A\U0000FE00}][{\U0000649D\U0000FE00}][{\U000064C4\U0000FE00}])"
    R"([{\U0000654F\U0000FE00}][{\U0000654F\U0000FE01}][{\U00006556\U0000FE00}])"
    R"([{\U0000656C\U0000FE00}][{\U00006578\U0000FE00}][{\U00006599\U0000FE00}])"
    R"([{\U000065C5\U0000FE00}][{\U000065E2\U0000FE00}][{\U000065E3\U0000FE00}])"
    R"([{\U00006613\U0000FE00}][{\U00006649\U0000FE00}][{\U00006674\U0000FE00}])"
    R"([{\U00006674\U0000FE01}][{\U00006688\U0000FE00}][{\U00006691\U0000FE00}])"
    R"([{\U00006691\U0000FE01}][{\U0000669C\U0000FE00}][{\U000066B4\U0000FE00}])"
    R"([{\U000066C6\U0000FE00}][{\U000066F4\U0000FE00}][{\U000066F8\U0000FE00}])"
    R"([{\U00006700\U0000FE00}][{\U00006717\U0000FE00}][{\U00006717\U0000FE01}])"
    R"([{\U00006717\U0000FE02}][{\U0000671B\U0000FE00}][{\U0000671B\U0000FE01}])"
    R"([{\U00006721\U0000FE00}][{\U0000674E\U0000FE00}][{\U00006753\U0000FE00}])"
    R"([{\U00006756\U0000FE00}][{\U0000675E\U0000FE00}][{\U0000677B\U0000FE00}])"
    R"([{\U00006785\U0000FE00}][{\U00006797\U0000FE00}][{\U000067F3\U0000FE00}])"
    R"([{\U000067FA\U0000FE00}][{\U00006817\U0000FE00}][{\U0000681F\U0000FE00}])"
    R"([{\U00006852\U0000FE00}][{\U00006881\U0000FE00}][{\U00006885\U0000FE00}])"
    R"([{\U00006885\U0000FE01}][{\U0000688E\U0000FE00}][{\U000068A8\U0000FE00}])"
    R"([{\U00006914\U0000FE00}][{\U00006942\U0000FE00}][{\U000069A3\U0000FE00}])"
    R"([{\U000069EA\U0000FE00}][{\U00006A02\U0000FE00}][{\U00006A02\U0000FE01}])"
    R"([{\U00006A02\U0000FE02}][{\U00006A13\U0000FE00}][{\U00006AA8\U0000FE00}])"
    R"([{\U00006AD3\U0000FE00}][{\U00006ADB\U0000FE00}][{\U00006B04\U0000FE00}])"
    R"([{\U00006B21\U0000FE00}][{\U00006B54\U0000FE00}][{\U00006B72\U0000FE00}])"
    R"([{\U00006B77\U0000FE00}][{\U00006B79\U0000FE00}][{\U00006B9F\U0000FE00}])"
    R"([{\U00006BAE\U0000FE00}][{\U00006BBA\U0000FE00}][{\U00006BBA\U0000FE01}])"
    R"([{\U00006BBA\U0000FE02}][{\U00006BBB\U0000FE00}][{\U00006C4E\U0000FE00}])"
    R"([{\U00006C67\U0000FE00}][{\U00006C88\U0000FE00}][{\U00006CBF\U0000FE00}])"
    R"([{\U00006CCC\U0000FE00}][{\U00006CCD\U0000FE00}][{\U00006CE5\U0000FE00}])"
    R"([{\U00006D16\U0000FE00}][{\U00006D1B\U0000FE00}][{\U00006D1E\U0000FE00}])"
    R"([{\U00006D34\U0000FE00}][{\U00006D3E\U0000FE00}][{\U00006D41\U0000FE00}])"
    R"([{\U00006D41\U0000FE01}][{\U00006D41\U0000FE02}][{\U00006D69\U0000FE00}])"
    R"([{\U00006D6A\U0000FE00}][{\U00006D77\U0000FE00}][{\U00006D77\U0000FE01}])"
    R"([{\U00006D78\U0000FE00}][{\U00006D85\U0000FE00}][{\U00006DCB\U0000FE00}])"
    R"([{\U00006DDA\U0000FE00}][{\U00006DEA\U0000FE00}][{\U00006DF9\U0000FE00}])"
    R"([{\U00006E1A\U0000FE00}][{\U00006E2F\U0000FE00}][{\U00006E6E\U0000FE00}])"
    R"([{\U00006E9C\U0000FE00}][{\U00006EBA\U0000FE00}][{\U00006EC7\U0000FE00}])"
    R"([{\U00006ECB\U0000FE00}][{\U00006ECB\U0000FE01}][{\U00006ED1\U0000FE00}])"
    R"([{\U00006EDB\U0000FE00}][{\U00006F0F\U0000FE00}][{\U00006F22\U0000FE00}])"
    R"([{\U00006F22\U0000FE01}][{\U00006F23\U0000FE00}][{\U00006F6E\U0000FE00}])"
    R"([{\U00006FC6\U0000FE00}][{\U00006FEB\U0000FE00}][{\U00006FFE\U0000FE00}])"
    R"([{\U0000701B\U0000FE00}][{\U0000701E\U0000FE00}][{\U0000701E\U0000FE01}])"
    R"([{\U00007039\U0000FE00}][{\U0000704A\U0000FE00}][{\U00007070\U0000FE00}])"
    R"([{\U00007077\U0000FE00}][{\U0000707D\U0000FE00}][{\U00007099\U0000FE00}])"
    R"([{\U000070AD\U0000FE00}][{\U000070C8\U0000FE00}][{\U000070D9\U0000FE00}])"
    R"([{\U00007145\U0000FE00}][{\U00007149\U0000FE00}][{\U0000716E\U0000FE00}])"
    R"([{\U0000716E\U0000FE01}][{\U0000719C\U0000FE00}][{\U000071CE\U0000FE00}])"
    R"([{\U000071D0\U0000FE00}][{\U00007210\U0000FE00}][{\U0000721B\U0000FE00}])"
    R"([{\U00007228\U0000FE00}][{\U0000722B\U0000FE00}][{\U00007235\U0000FE00}])"
    R"([{\U00007235\U0000FE01}][{\U00007250\U0000FE00}][{\U00007262\U0000FE00}])"
    R"([{\U00007280\U0000FE00}][{\U00007295\U0000FE00}][{\U000072AF\U0000FE00}])"
    R"([{\U000072C0\U0000FE00}][{\U000072FC\U0000FE00}][{\U0000732A\U0000FE00}])"
    R"([{\U0000732A\U0000FE01}][{\U00007375\U0000FE00}][{\U0000737A\U0000FE00}])"
    R"([{\U00007387\U0000FE00}][{\U00007387\U0000FE01}][{\U0000738B\U0000FE00}])"
    R"([{\U000073A5\U0000FE00}][{\U000073B2\U0000FE00}][{\U000073DE\U0000FE00}])"
    R"([{\U00007406\U0000FE00}][{\U00007409\U0000FE00}][{\U00007422\U0000FE00}])"
    R"([{\U00007447\U0000FE00}][{\U0000745C\U0000FE00}][{\U00007469\U0000FE00}])"
    R"([{\U00007471\U0000FE00}][{\U00007471\U0000FE01}][{\U00007485\U0000FE00}])"
    R"([{\U00007489\U0000FE00}][{\U00007498\U0000FE00}][{\U000074CA\U0000FE00}])"
    R"([{\U00007506\U0000FE00}][{\U00007524\U0000FE00}][{\U0000753B\U0000FE00}])"
    R"([{\U0000753E\U0000FE00}][{\U00007559\U0000FE00}][{\U00007565\U0000FE00}])"
    R"([{\U00007570\U0000FE00}][{\U00007570\U0000FE01}][{\U000075E2\U0000FE00}])"
    R"([{\U00007610\U0000FE00}][{\U0000761D\U0000FE00}][{\U0000761F\U0000FE00}])"
    R"([{\U00007642\U0000FE00}][{\U00007669\U0000FE00}][{\U000076CA\U0000FE00}])"
    R"([{\U000076CA\U0000FE01}][{\U000076DB\U0000FE00}][{\U000076E7\U0000FE00}])"
    R"([{\U000076F4\U0000FE00}][{\U000076F4\U0000FE01}][{\U00007701\U0000FE00}])"
    R"([{\U0000771E\U0000FE00}][{\U0000771F\U0000FE00}][{\U0000771F\U0000FE01}])"
    R"([{\U00007740\U0000FE00}][{\U0000774A\U0000FE00}][{\U0000774A\U0000FE01}])"
    R"([{\U0000778B\U0000FE00}][{\U000077A7\U0000FE00}][{\U0000784E\U0000FE00}])"
    R"([{\U0000786B\U0000FE00}][{\U0000788C\U0000FE00}][{\U0000788C\U0000FE01}])"
    R"([{\U00007891\U0000FE00}][{\U000078CA\U0000FE00}][{\U000078CC\U0000FE00}])"
    R"([{\U000078CC\U0000FE01}][{\U000078FB\U0000FE00}][{\U0000792A\U0000FE00}])"
    R"([{\U0000793C\U0000FE00}][{\U0000793E\U0000FE00}][{\U00007948\U0000FE00}])"
    R"([{\U00007949\U0000FE00}][{\U00007950\U0000FE00}][{\U00007956\U0000FE00}])"
    R"([{\U00007956\U0000FE01}][{\U0000795D\U0000FE00}][{\U0000795E\U0000FE00}])"
    R"([{\U00007965\U0000FE00}][{\U0000797F\U0000FE00}][{\U0000798D\U0000FE00}])"
    R"([{\U0000798E\U0000FE00}][{\U0000798F\U0000FE00}][{\U0000798F\U0000FE01}])"
    R"([{\U000079AE\U0000FE00}][{\U000079CA\U0000FE00}][{\U000079EB\U0000FE00}])"
    R"([{\U00007A1C\U0000FE00}][{\U00007A40\U0000FE00}][{\U00007A40\U0000FE01}])"
    R"([{\U00007A4A\U0000FE00}][{\U00007A4F\U0000FE00}][{\U00007A81\U0000FE00}])"
    R"([{\U00007AB1\U0000FE00}][{\U00007ACB\U0000FE00}][{\U00007AEE\U0000FE00}])"
    R"([{\U00007B20\U0000FE00}][{\U00007BC0\U0000FE00}][{\U00007BC0\U0000FE01}])"
    R"([{\U00007BC6\U0000FE00}][{\U00007BC9\U0000FE00}][{\U00007C3E\U0000FE00}])"
    R"([{\U00007C60\U0000FE00}][{\U00007C7B\U0000FE00}][{\U00007C92\U0000FE00}])"
    R"([{\U00007CBE\U0000FE00}][{\U00007CD2\U0000FE00}][{\U00007CD6\U0000FE00}])"
    R"([{\U00007CE3\U0000FE00}][{\U00007CE7\U0000FE00}][{\U00007CE8\U0000FE00}])"
    R"([{\U00007D00\U0000FE00}][{\U00007D10\U0000FE00}][{\U00007D22\U0000FE00}])"
    R"([{\U00007D2F\U0000FE00}][{\U00007D5B\U0000FE00}][{\U00007D63\U0000FE00}])"
    R"([{\U00007DA0\U0000FE00}][{\U00007DBE\U0000FE00}][{\U00007DC7\U0000FE00}])"
    R"([{\U00007DF4\U0000FE00}][{\U00007DF4\U0000FE01}][{\U00007DF4\U0000FE02}])"
    R"([{\U00007E02\U0000FE00}][{\U00007E09\U0000FE00}][{\U00007E37\U0000FE00}])"
    R"([{\U00007E41\U0000FE00}][{\U00007E45\U0000FE00}][{\U00007F3E\U0000FE00}])"
    R"([{\U00007F72\U0000FE00}][{\U00007F79\U0000FE00}][{\U00007F7A\U0000FE00}])"
    R"([{\U00007F85\U0000FE00}][{\U00007F95\U0000FE00}][{\U00007F9A\U0000FE00}])"
    R"([{\U00007FBD\U0000FE00}][{\U00007FFA\U0000FE00}][{\U00008001\U0000FE00}])"
    R"([{\U00008005\U0000FE00}][{\U00008005\U0000FE01}][{\U00008005\U0000FE02}])"
    R"([{\U00008046\U0000FE00}][{\U00008060\U0000FE00}][{\U0000806F\U0000FE00}])"
    R"([{\U00008070\U0000FE00}][{\U0000807E\U0000FE00}][{\U0000808B\U0000FE00}])"
    R"([{\U000080AD\U0000FE00}][{\U000080B2\U0000FE00}][{\U00008103\U0000FE00}])"
    R"([{\U0000813E\U0000FE00}][{\U000081D8\U0000FE00}][{\U000081E8\U0000FE00}])"
    R"([{\U000081ED\U0000FE00}][{\U00008201\U0000FE00}][{\U00008201\U0000FE01}])"
    R"([{\U00008204\U0000FE00}][{\U00008218\U0000FE00}][{\U0000826F\U0000FE00}])"
    R"([{\U00008279\U0000FE00}][{\U00008279\U0000FE01}][{\U0000828B\U0000FE00}])"
    R"([{\U00008291\U0000FE00}][{\U0000829D\U0000FE00}][{\U000082B1\U0000FE00}])"
    R"([{\U000082B3\U0000FE00}][{\U000082BD\U0000FE00}][{\U000082E5\U0000FE00}])"
    R"([{\U000082E5\U0000FE01}][{\U000082E6\U0000FE00}][{\U0000831D\U0000FE00}])"
    R"([{\U00008323\U0000FE00}][{\U00008336\U0000FE00}][{\U00008352\U0000FE00}])"
    R"([{\U00008353\U0000FE00}][{\U00008363\U0000FE00}][{\U000083AD\U0000FE00}])"
    R"([{\U000083BD\U0000FE00}][{\U000083C9\U0000FE00}][{\U000083CA\U0000FE00}])"
    R"([{\U000083CC\U0000FE00}][{\U000083DC\U0000FE00}][{\U000083E7\U0000FE00}])"
    R"([{\U000083EF\U0000FE00}][{\U000083F1\U0000FE00}][{\U0000843D\U0000FE00}])"
    R"([{\U00008449\U0000FE00}][{\U00008457\U0000FE00}][{\U00008457\U0000FE01}])"
    R"([{\U000084EE\U0000FE00}][{\U000084F1\U0000FE00}][{\U000084F3\U0000FE00}])"
    R"([{\U000084FC\U0000FE00}][{\U00008516\U0000FE00}][{\U00008564\U0000FE00}])"
    R"([{\U000085CD\U0000FE00}][{\U000085FA\U0000FE00}][{\U00008606\U0000FE00}])"
    R"([{\U00008612\U0000FE00}][{\U0000862D\U0000FE00}][{\U0000863F\U0000FE00}])"
    R"([{\U00008650\U0000FE00}][{\U0000865C\U0000FE00}][{\U0000865C\U0000FE01}])"
    R"([{\U00008667\U0000FE00}][{\U00008669\U0000FE00}][{\U00008688\U0000FE00}])"
    R"([{\U000086A9\U0000FE00}][{\U000086E2\U0000FE00}][{\U0000870E\U0000FE00}])"
    R"([{\U00008728\U0000FE00}][{\U0000876B\U0000FE00}][{\U00008779\U0000FE00}])"
    R"([{\U00008779\U0000FE01}][{\U00008786\U0000FE00}][{\U000087BA\U0000FE00}])"
    R"([{\U000087E1\U0000FE00}][{\U00008801\U0000FE00}][{\U0000881F\U0000FE00}])"
    R"([{\U0000884C\U0000FE00}][{\U00008860\U0000FE00}][{\U00008863\U0000FE00}])"
    R"([{\U000088C2\U0000FE00}][{\U000088CF\U0000FE00}][{\U000088D7\U0000FE00}])"
    R"([{\U000088DE\U0000FE00}][{\U000088E1\U0000FE00}][{\U000088F8\U0000FE00}])"
    R"([{\U000088FA\U0000FE00}][{\U00008910\U0000FE00}][{\U00008941\U0000FE00}])"
    R"([{\U00008964\U0000FE00}][{\U00008986\U0000FE00}][{\U0000898B\U0000FE00}])"
    R"([{\U00008996\U0000FE00}][{\U00008996\U0000FE01}][{\U00008AA0\U0000FE00}])"
    R"([{\U00008AAA\U0000FE00}][{\U00008AAA\U0000FE01}][{\U00008ABF\U0000FE00}])"
    R"([{\U00008ACB\U0000FE00}][{\U00008AD2\U0000FE00}][{\U00008AD6\U0000FE00}])"
    R"([{\U00008AED\U0000FE00}][{\U00008AED\U0000FE01}][{\U00008AF8\U0000FE00}])"
    R"([{\U00008AF8\U0000FE01}][{\U00008AFE\U0000FE00}][{\U00008AFE\U0000FE01}])"
    R"([{\U00008B01\U0000FE00}][{\U00008B01\U0000FE01}][{\U00008B39\U0000FE00}])"
    R"([{\U00008B39\U0000FE01}][{\U00008B58\U0000FE00}][{\U00008B80\U0000FE00}])"
    R"([{\U00008B8A\U0000FE00}][{\U00008B8A\U0000FE01}][{\U00008C48\U0000FE00}])"
    R"([{\U00008C55\U0000FE00}][{\U00008CAB\U0000FE00}][{\U00008CC1\U0000FE00}])"
    R"([{\U00008CC2\U0000FE00}][{\U00008CC8\U0000FE00}][{\U00008CD3\U0000FE00}])"
    R"([{\U00008D08\U0000FE00}][{\U00008D08\U0000FE01}][{\U00008D1B\U0000FE00}])"
    R"([{\U00008D77\U0000FE00}][{\U00008DBC\U0000FE00}][{\U00008DCB\U0000FE00}])"
    R"([{\U00008DEF\U0000FE00}][{\U00008DF0\U0000FE00}][{\U00008ECA\U0000FE00}])"
    R"([{\U00008ED4\U0000FE00}][{\U00008F26\U0000FE00}][{\U00008F2A\U0000FE00}])"
    R"([{\U00008F38\U0000FE00}][{\U00008F38\U0000FE01}][{\U00008F3B\U0000FE00}])"
    R"([{\U00008F62\U0000FE00}][{\U00008F9E\U0000FE00}][{\U00008FB0\U0000FE00}])"
    R"([{\U00008FB6\U0000FE00}][{\U00009023\U0000FE00}][{\U00009038\U0000FE00}])"
    R"([{\U00009038\U0000FE01}][{\U00009072\U0000FE00}][{\U0000907C\U0000FE00}])"
    R"([{\U0000908F\U0000FE00}][{\U00009094\U0000FE00}][{\U000090CE\U0000FE00}])"
    R"([{\U000090DE\U0000FE00}][{\U000090F1\U0000FE00}][{\U000090FD\U0000FE00}])"
    R"([{\U00009111\U0000FE00}][{\U0000911B\U0000FE00}][{\U0000916A\U0000FE00}])"
    R"([{\U00009199\U0000FE00}][{\U000091B4\U0000FE00}][{\U000091CC\U0000FE00}])"
    R"([{\U000091CF\U0000FE00}][{\U000091D1\U0000FE00}][{\U00009234\U0000FE00}])"
    R"([{\U00009238\U0000FE00}][{\U00009276\U0000FE00}][{\U0000927C\U0000FE00}])"
    R"([{\U000092D7\U0000FE00}][{\U000092D8\U0000FE00}][{\U00009304\U0000FE00}])"
    R"([{\U0000934A\U0000FE00}][{\U000093F9\U0000FE00}][{\U00009415\U0000FE00}])"
    R"([{\U0000958B\U0000FE00}][{\U000095AD\U0000FE00}][{\U000095B7\U0000FE00}])"
    R"([{\U0000962E\U0000FE00}][{\U0000964B\U0000FE00}][{\U0000964D\U0000FE00}])"
    R"([{\U00009675\U0000FE00}][{\U00009678\U0000FE00}][{\U0000967C\U0000FE00}])"
    R"([{\U00009686\U0000FE00}][{\U000096A3\U0000FE00}][{\U000096B7\U0000FE00}])"
    R"([{\U000096B8\U0000FE00}][{\U000096C3\U0000FE00}][{\U000096E2\U0000FE00}])"
    R"([{\U000096E3\U0000FE00}][{\U000096E3\U0000FE01}][{\U000096F6\U0000FE00}])"
    R"([{\U000096F7\U0000FE00}][{\U00009723\U0000FE00}][{\U00009732\U0000FE00}])"
    R"([{\U00009748\U0000FE00}][{\U00009756\U0000FE00}][{\U00009756\U0000FE01}])"
    R"([{\U000097DB\U0000FE00}][{\U000097E0\U0000FE00}][{\U000097FF\U0000FE00}])"
    R"([{\U000097FF\U0000FE01}][{\U0000980B\U0000FE00}][{\U0000980B\U0000FE01}])"
    R"([{\U0000980B\U0000FE02}][{\U00009818\U0000FE00}][{\U00009829\U0000FE00}])"
    R"([{\U0000983B\U0000FE00}][{\U0000983B\U0000FE01}][{\U0000985E\U0000FE00}])"
    R"([{\U000098E2\U0000FE00}][{\U000098EF\U0000FE00}][{\U000098FC\U0000FE00}])"
    R"([{\U00009928\U0000FE00}][{\U00009929\U0000FE00}][{\U000099A7\U0000FE00}])"
    R"([{\U000099C2\U0000FE00}][{\U000099F1\U0000FE00}][{\U000099FE\U0000FE00}])"
    R"([{\U00009A6A\U0000FE00}][{\U00009B12\U0000FE00}][{\U00009B12\U0000FE01}])"
    R"([{\U00009B6F\U0000FE00}][{\U00009C40\U0000FE00}][{\U00009C57\U0000FE00}])"
    R"([{\U00009CFD\U0000FE00}][{\U00009D67\U0000FE00}][{\U00009DB4\U0000FE00}])"
    R"([{\U00009DFA\U0000FE00}][{\U00009E1E\U0000FE00}][{\U00009E7F\U0000FE00}])"
    R"([{\U00009E97\U0000FE00}][{\U00009E9F\U0000FE00}][{\U00009EBB\U0000FE00}])"
    R"([{\U00009ECE\U0000FE00}][{\U00009EF9\U0000FE00}][{\U00009EFE\U0000FE00}])"
    R"([{\U00009F05\U0000FE00}][{\U00009F0F\U0000FE00}][{\U00009F16\U0000FE00}])"
    R"([{\U00009F3B\U0000FE00}][{\U00009F43\U0000FE00}][{\U00009F8D\U0000FE00}])"
    R"([{\U00009F8E\U0000FE00}][{\U00009F9C\U0000FE00}][{\U00009F9C\U0000FE01}])"
    R"([{\U00009F9C\U0000FE02}][{\U000020122\U0000FE00}][{\U00002051C\U0000FE00}])"
    R"([{\U000020525\U0000FE00}][{\U00002054B\U0000FE00}][{\U00002063A\U0000FE00}])"
    R"([{\U000020804\U0000FE00}][{\U0000208DE\U0000FE00}][{\U000020A2C\U0000FE00}])"
    R"([{\U000020B63\U0000FE00}][{\U0000214E4\U0000FE00}][{\U0000216A8\U0000FE00}])"
    R"([{\U0000216EA\U0000FE00}][{\U0000219C8\U0000FE00}][{\U000021B18\U0000FE00}])"
    R"([{\U000021D0B\U0000FE00}][{\U000021DE4\U0000FE00}][{\U000021DE6\U0000FE00}])"
    R"([{\U000022183\U0000FE00}][{\U00002219F\U0000FE00}][{\U000022331\U0000FE00}])"
    R"([{\U000022331\U0000FE01}][{\U0000226D4\U0000FE00}][{\U000022844\U0000FE00}])"
    R"([{\U00002284A\U0000FE00}][{\U000022B0C\U0000FE00}][{\U000022BF1\U0000FE00}])"
    R"([{\U00002300A\U0000FE00}][{\U0000232B8\U0000FE00}][{\U00002335F\U0000FE00}])"
    R"([{\U000023393\U0000FE00}][{\U00002339C\U0000FE00}][{\U0000233C3\U0000FE00}])"
    R"([{\U0000233D5\U0000FE00}][{\U00002346D\U0000FE00}][{\U0000236A3\U0000FE00}])"
    R"([{\U0000238A7\U0000FE00}][{\U000023A8D\U0000FE00}][{\U000023AFA\U0000FE00}])"
    R"([{\U000023CBC\U0000FE00}][{\U000023D1E\U0000FE00}][{\U000023ED1\U0000FE00}])"
    R"([{\U000023F5E\U0000FE00}][{\U000023F8E\U0000FE00}][{\U000024263\U0000FE00}])"
    R"([{\U0000242EE\U0000FE00}][{\U0000243AB\U0000FE00}][{\U000024608\U0000FE00}])"
    R"([{\U000024735\U0000FE00}][{\U000024814\U0000FE00}][{\U000024C36\U0000FE00}])"
    R"([{\U000024C92\U0000FE00}][{\U000024FA1\U0000FE00}][{\U000024FB8\U0000FE00}])"
    R"([{\U000025044\U0000FE00}][{\U0000250F2\U0000FE00}][{\U0000250F3\U0000FE00}])"
    R"([{\U000025119\U0000FE00}][{\U000025133\U0000FE00}][{\U000025249\U0000FE00}])"
    R"([{\U00002541D\U0000FE00}][{\U000025626\U0000FE00}][{\U00002569A\U0000FE00}])"
    R"([{\U0000256C5\U0000FE00}][{\U00002597C\U0000FE00}][{\U000025AA7\U0000FE00}])"
    R"([{\U000025AA7\U0000FE01}][{\U000025BAB\U0000FE00}][{\U000025C80\U0000FE00}])"
    R"([{\U000025CD0\U0000FE00}][{\U000025F86\U0000FE00}][{\U0000261DA\U0000FE00}])"
    R"([{\U000026228\U0000FE00}][{\U000026247\U0000FE00}][{\U0000262D9\U0000FE00}])"
    R"([{\U00002633E\U0000FE00}][{\U0000264DA\U0000FE00}][{\U000026523\U0000FE00}])"
    R"([{\U0000265A8\U0000FE00}][{\U0000267A7\U0000FE00}][{\U0000267B5\U0000FE00}])"
    R"([{\U000026B3C\U0000FE00}][{\U000026C36\U0000FE00}][{\U000026CD5\U0000FE00}])"
    R"([{\U000026D6B\U0000FE00}][{\U000026F2C\U0000FE00}][{\U000026FB1\U0000FE00}])"
    R"([{\U0000270D2\U0000FE00}][{\U0000273CA\U0000FE00}][{\U000027667\U0000FE00}])"
    R"([{\U0000278AE\U0000FE00}][{\U000027966\U0000FE00}][{\U000027CA8\U0000FE00}])"
    R"([{\U000027ED3\U0000FE00}][{\U000027F2F\U0000FE00}][{\U0000285D2\U0000FE00}])"
    R"([{\U0000285ED\U0000FE00}][{\U00002872E\U0000FE00}][{\U000028BFA\U0000FE00}])"
    R"([{\U000028D77\U0000FE00}][{\U000029145\U0000FE00}][{\U0000291DF\U0000FE00}])"
    R"([{\U00002921A\U0000FE00}][{\U00002940A\U0000FE00}][{\U000029496\U0000FE00}])"
    R"([{\U0000295B6\U0000FE00}][{\U000029B30\U0000FE00}][{\U00002A0CE\U0000FE00}])"
    R"([{\U00002A105\U0000FE00}][{\U00002A20E\U0000FE00}][{\U00002A291\U0000FE00}])"
    R"([{\U00002A392\U0000FE00}][{\U00002A600\U0000FE00}]])";

bool Character::IsStandardizedVariationSequence(UChar32 ch, UChar32 vs) {
  // Avoid making extra calls if no variation selector is provided or if
  // provided variation selector is emoji/text (VS15/VS16) variation
  // selector.
  if (!vs || IsUnicodeEmojiVariationSelector(vs)) {
    return false;
  }
  DEFINE_THREAD_SAFE_STATIC_LOCAL(icu::UnicodeSet,
                                  standardizedVariationSequencesSet, ());
  ApplyPatternAndFreezeIfEmpty(&standardizedVariationSequencesSet,
                               kStandardizedVariationSequences);
  icu::UnicodeString variation_sequence =
      (icu::UnicodeString)ch + (icu::UnicodeString)vs;
  return standardizedVariationSequencesSet.contains(variation_sequence);
}

bool Character::IsEmojiVariationSequence(UChar32 ch, UChar32 vs) {
  return IsUnicodeEmojiVariationSelector(vs) && IsEmoji(ch);
}

// From UTS #37 (https://unicode.org/reports/tr37/): An Ideographic Variation
// Sequence (IVS) is a sequence of two coded characters, the first being a
// character with the Ideographic property that is not canonically nor
// compatibly decomposable, the second being a variation selector character in
// the range U+E0100 to U+E01EF.
bool Character::IsIdeographicVariationSequence(UChar32 ch, UChar32 vs) {
  // Check variation selector fist to avoid making extra icu calls.
  if (!IsInRange(vs, 0xE0100, 0xE01EF)) {
    return false;
  }
  unicode::CharDecompositionType decomp_type = unicode::DecompositionType(ch);
  return u_hasBinaryProperty(ch, UCHAR_IDEOGRAPHIC) &&
         decomp_type != unicode::kDecompositionCanonical &&
         decomp_type != unicode::kDecompositionCompat;
}

bool Character::IsVariationSequence(UChar32 ch, UChar32 vs) {
  return IsEmojiVariationSequence(ch, vs) ||
         IsStandardizedVariationSequence(ch, vs) ||
         IsIdeographicVariationSequence(ch, vs);
}

}  // namespace blink
