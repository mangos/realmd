/**
 * MaNGOS is a full featured server for World of Warcraft, supporting
 * the following clients: 1.12.x, 2.4.3, 3.3.5a, 4.3.4a and 5.4.8
 *
 * Copyright (C) 2005-2026 MaNGOS <https://www.getmangos.eu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * World of Warcraft, and all World of Warcraft or Warcraft art, images,
 * and lore are copyrighted by Blizzard Entertainment, Inc.
 */

/** \file
 \ingroup realmd
 */

#include "AccountNameFold.h"

#include <cstddef>

namespace MaNGOS::Realmd
{
    namespace
    {
        /// True for a UTF-8 continuation byte, 0b10xxxxxx.
        bool IsContinuation(unsigned char byte)
        {
            return (byte & 0xC0) == 0x80;
        }

        /**
         * @brief Bytes claimed by @p lead, or 1 if it is not a legal lead.
         *
         * 0xC0 and 0xC1 are excluded: they can only ever introduce an overlong
         * two-byte form. 0xF5..0xFF are excluded: they would encode beyond
         * U+10FFFF. Both therefore count as one stray byte apiece.
         */
        std::size_t ClaimedLength(unsigned char lead)
        {
            if ((lead & 0xE0) == 0xC0 && lead >= 0xC2)
            {
                return 2;
            }

            if ((lead & 0xF0) == 0xE0)
            {
                return 3;
            }

            if ((lead & 0xF8) == 0xF0 && lead <= 0xF4)
            {
                return 4;
            }

            return 1;
        }
    }

    std::string FoldAccountName(std::string const& name)
    {
        std::string folded;
        folded.reserve(name.size());

        for (std::size_t index = 0; index < name.size(); )
        {
            unsigned char const lead =
                static_cast<unsigned char>(name[index]);

            if (lead >= 0x20 && lead < 0x7F)
            {
                folded += static_cast<char>(lead);
                ++index;
                continue;
            }

            // Everything else folds to exactly one '?'. How many bytes that
            // '?' stands for is settled only once the whole sequence has been
            // checked: a lead byte that does not get the continuation bytes it
            // promised is not a code point, it is one bad byte, and consuming
            // what follows it would hide a legitimate character from the
            // operator reading the log.
            std::size_t length = ClaimedLength(lead);

            if (length > 1)
            {
                bool valid = (name.size() - index >= length);

                for (std::size_t offset = 1; valid && offset < length; ++offset)
                {
                    if (!IsContinuation(
                            static_cast<unsigned char>(name[index + offset])))
                    {
                        valid = false;
                    }
                }

                if (!valid)
                {
                    length = 1;
                }
            }

            folded += '?';
            index += length;
        }

        return folded;
    }
}
