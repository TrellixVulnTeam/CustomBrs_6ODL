/*
 * Copyright (C) 2000 Lars Knoll (knoll@kde.org)
 * Copyright (C) 2003, 2004, 2006, 2007, 2008, 2009, 2010, 2011 Apple Inc. All right reserved.
 * Copyright (C) 2010 Google Inc. All rights reserved.
 * Copyright (C) 2014 Adobe Systems Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifndef TrailingObjects_h
#define TrailingObjects_h

#include "wtf/Vector.h"

namespace WebCore {

class InlineIterator;
class RenderBox;
class RenderText;

struct BidiRun;

template <class Iterator, class Run> class BidiResolver;
template <class Iterator> class MidpointState;
typedef BidiResolver<InlineIterator, BidiRun> InlineBidiResolver;
typedef MidpointState<InlineIterator> LineMidpointState;

class TrailingObjects {
public:
    TrailingObjects()
        : m_whitespace(0)
    {
    }

    void setTrailingWhitespace(RenderText* whitespace)
    {
        ASSERT(whitespace);
        m_whitespace = whitespace;
    }

    void clear()
    {
        m_whitespace = 0;
        // Using resize(0) rather than clear() here saves 2% on
        // PerformanceTests/Layout/line-layout.html because we avoid freeing and
        // re-allocating the underlying buffer repeatedly.
        m_boxes.resize(0);
    }

    void appendBoxIfNeeded(RenderBox* box)
    {
        if (m_whitespace)
            m_boxes.append(box);
    }

    enum CollapseFirstSpaceOrNot { DoNotCollapseFirstSpace, CollapseFirstSpace };

    void updateMidpointsForTrailingBoxes(LineMidpointState&, const InlineIterator& lBreak, CollapseFirstSpaceOrNot);

private:
    RenderText* m_whitespace;
    Vector<RenderBox*, 4> m_boxes;
};

}

#endif // TrailingObjects_h
