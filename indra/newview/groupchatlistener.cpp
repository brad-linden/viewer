/**
 * @file   groupchatlistener.cpp
 * @author Nat Goodspeed
 * @date   2011-04-11
 * @brief  Implementation for LLGroupChatListener.
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2024, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

// Precompiled header
#include "llviewerprecompiledheaders.h"
// associated header
#include "groupchatlistener.h"
// STL headers
// std headers
// external library headers
// other Linden headers
#include "llchat.h"
#include "llgroupactions.h"
#include "llimview.h"

static const F32 GROUP_CHAT_THROTTLE_PERIOD = 1.f;

LLGroupChatListener::LLGroupChatListener():
    LLEventAPI("GroupChat",
               "API to enter, leave, send and intercept group chat messages"),
    mLastThrottleTime(0)
{
    add("startGroupChat",
        "Enter a group chat in group with UUID [\"group_id\"]\n"
        "Assumes the logged-in agent is already a member of this group.",
        &LLGroupChatListener::startGroupChat,
        llsd::map("group_id", LLSD()));
    add("leaveGroupChat",
        "Leave a group chat in group with UUID [\"group_id\"]\n"
        "Assumes a prior successful startIM request.",
        &LLGroupChatListener::leaveGroupChat,
        llsd::map("group_id", LLSD()));
    add("sendGroupIM",
        "send a groupchat IM",
        &LLGroupChatListener::sendGroupIM,
        llsd::map("message", LLSD(), "group_id", LLSD()));
}

bool is_in_group(LLEventAPI::Response &response, const LLSD &data)
{
    if (!LLGroupActions::isInGroup(data["group_id"]))
    {
        response.error(stringize("You are not the member of the group:", std::quoted(data["group_id"].asString())));
        return false;
    }
    return true;
}

void LLGroupChatListener::startGroupChat(LLSD const &data)
{
    Response response(LLSD(), data);
    if (!is_in_group(response, data))
    {
        return;
    }
    if (LLGroupActions::startIM(data["group_id"]).isNull())
    {
        return response.error(stringize("Failed to start group chat session ", std::quoted(data["group_id"].asString())));
    }
}

void LLGroupChatListener::leaveGroupChat(LLSD const &data)
{
    Response response(LLSD(), data);
    if (is_in_group(response, data))
    {
        LLGroupActions::endIM(data["group_id"].asUUID());
    }
}

void LLGroupChatListener::sendGroupIM(LLSD const &data)
{
    Response response(LLSD(), data);
    if (!is_in_group(response, data))
    {
        return;
    }

    F64 cur_time = LLTimer::getElapsedSeconds();

    if (cur_time < mLastThrottleTime + GROUP_CHAT_THROTTLE_PERIOD)
    {
        LL_DEBUGS("LLGroupChatListener") << "'sendGroupIM' was  throttled" << LL_ENDL;
        return;
    }
    mLastThrottleTime = cur_time;

    LLUUID group_id(data["group_id"]);
    LLIMModel::sendMessage(LUA_PREFIX + data["message"].asString(),
                           gIMMgr->computeSessionID(IM_SESSION_GROUP_START, group_id),
                           group_id,
                           IM_SESSION_GROUP_START);
}
