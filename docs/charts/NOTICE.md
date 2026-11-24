# src/charts — origin and licences

src/charts (what Data › Technical data uses; the Charts tab itself is not part of Deepwater Mark 3) is a C++ port of
**Quant Terminal**, used under its MIT licence:

    MIT License
    Copyright (c) 2026 Lewis Jackson

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the "Software"), to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
    to permit persons to whom the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial portions
    of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
    THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF
    CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.

JavaScript runs in **QuickJS-ng** v0.18.0 (third_party/quickjs-ng, MIT License, Copyright (c)
2017-2026 Fabrice Bellard, 2017-2024 Charlie Gordon, 2023-2026 Ben Noordhuis, 2023-2026 Saúl Ibarra Corretgé; full
text in third_party/quickjs-ng/LICENSE, origin in third_party/quickjs-ng/SOURCES.md).

Pine Script® runs in **PineTS** 0.13.1 (third_party/pinets, by LuxAlgo / QuantForge, **AGPL-3.0-only**; full text in
third_party/pinets/LICENSE, origin in third_party/pinets/SOURCES.md), inside QuickJS-ng. Because the app ships it,
Deepwater as a whole is distributed under the AGPL-3.0 (LICENSE at the repo root; the user's choice, 2026-11-18).
Pine Script® is a trademark of TradingView, Inc.; Deepwater and PineTS are not affiliated with TradingView.

Quant Terminal draws its charts with TradingView Lightweight Charts (Apache 2.0). The port does **not** use or port
that library: the chart is Deepwater's own QPainter renderer, so no TradingView code or logo is included.
Deepwater is not affiliated with Quant Terminal, Part-Time Quant or TradingView.
Left out on purpose (user's choice, 2026-11-16): the Part-Time Quant Academy ribbon, the Help link to
part-timequant.com and the PTQ tab / connector.
