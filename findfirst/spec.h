/*
 * Copyright (C) 2011 Mathieu Turcotte (mathieuturcotte.ca)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see http://www.gnu.org/licenses/.
 */

#ifndef SPEC_H_
#define SPEC_H_

/*
 * Matches the regular language accepted by findfirst/findnext. More
 * precisely, * matches zero or more characters and ? matches any
 * characters, but only one. Every other characters match themself.
 * To respect the Windows behavior, *.* matches everything.
 */
int match_spec(const char* spec, const char* text);

#endif /* SPEC_H_ */

