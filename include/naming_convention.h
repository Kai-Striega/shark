//
// ICRAR - International Centre for Radio Astronomy Research
// (c) UWA - The University of Western Australia, 2018
// Copyright by UWA (in the framework of the ICRAR)
//
// This program is free software: you can redistribute it and/or modify
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
// along with this program.  If not, see <https://www.gnu.org/licenses/>.
//

/**
 * @file
 *
 * Naming conventions used in shark
 */

#ifndef INCLUDE_NAMING_CONVENTION_H_
#define INCLUDE_NAMING_CONVENTION_H_

#include <string>
#include <iosfwd>

namespace shark {

enum struct naming_convention {
	NONE,
	SNAKE_CASE,
	CAMEL_CASE,
	LOWER_CAMEL_CASE
};

template <typename CharT, typename Traits>
std::basic_ostream<CharT, Traits> &operator<<(std::basic_ostream<CharT, Traits> &os, const naming_convention convention)
{
	if (convention == naming_convention::NONE) {
		os << "<none>";
	}
	else if (convention == naming_convention::SNAKE_CASE) {
		os << "snake_case";
	}
	else if (convention == naming_convention::CAMEL_CASE) {
		os << "CamelCase";
	}
	else if (convention == naming_convention::LOWER_CAMEL_CASE) {
		os << "lowerCamelCase";
	}
	return os;
}


/**
 * Returns whether the given @a word follows the given naming convention or not.
 *
 * @param word The word to check
 * @return Whether the word follows the given naming convention or not.
 */
bool follows_convention(const std::string &word, const naming_convention convention);

}

#endif /* INCLUDE_NAMING_CONVENTION_H_ */