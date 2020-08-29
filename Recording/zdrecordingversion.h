/******************************************************************************
    Copyright (C) 2020 by Zaodao(Dalian) Education Technology Co., Ltd..

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#pragma once

#define ZDRECORDING_VERSION_MAJOR  1
#define ZDRECORDING_VERSION_MINOR  0
#define ZDRECORDING_VERSION_PATCH  0

#define VERSION_STR(x) VERSION_FMT(x)
#define VERSION_FMT(x) #x

#define ZDRECORDING_VERSION        ZDRECORDING_VERSION_MAJOR.ZDRECORDING_VERSION_MINOR.ZDRECORDING_VERSION_PATCH
#define ZDRECORDING_VERSION_STR    VERSION_STR(ZDRECORDING_VERSION)

#define ZDRECORDING_FILE_VERSION          ZDRECORDING_VERSION.0
#define ZDRECORDING_FILE_VERSION_STR      VERSION_STR(ZDRECORDING_FILE_VERSION)
#define ZDRECORDING_PRODUCT_VERSION_STR   ZDRECORDING_FILE_VERSION_STR
