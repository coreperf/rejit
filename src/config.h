// Copyright (C) 2013 Alexandre Rames <alexandre@uop.re>

// rejit is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef REJIT_CONFIG_H_
#define REJIT_CONFIG_H_

// Uncomment/comment options below to enable/disable them.

// The following patterns are not defined in the ISO-IEC 9945, but are widely
// supported and used.
// Allow support for them.
//  \d   : Digit character; equivalent to [0-9].
//  \D   : Non digit character; equivalent to [^0-9].
//  \n
//  \r
//  \s   : Whitespace character (space or tab).
//  \S   : Non whitespace character.
//  \t   : Tab character.
//  \xAB : Character with hexadecimal code 0xAB.
// TODO: Add support for:
//  \<, \>
#define ENABLE_COMMON_ESCAPED_PATTERNS

#endif
