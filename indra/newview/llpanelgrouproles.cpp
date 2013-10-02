/** 
 * @file llpanelgrouproles.cpp
 * @brief Panel for roles information about a particular group.
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"

#include "llcheckboxctrl.h"

#include "llagent.h"
#include "llavatarnamecache.h"
#include "llbutton.h"
#include "llfiltereditor.h"
#include "llfloatergroupbulkban.h"
#include "llfloatergroupinvite.h"
#include "llavataractions.h"
#include "lliconctrl.h"
#include "lllineeditor.h"
#include "llnamelistctrl.h"
#include "llnotifications.h"
#include "llnotificationsutil.h"
#include "llpanelgrouproles.h"
#include "llscrolllistctrl.h"
#include "llscrolllistitem.h"
#include "llscrolllistcell.h"
#include "llslurl.h"
#include "lltabcontainer.h"
#include "lltextbox.h"
#include "lltexteditor.h"
#include "lltrans.h"
#include "llviewertexturelist.h"
#include "llviewerwindow.h"
#include "llfocusmgr.h"
#include "llviewercontrol.h"

#include "roles_constants.h"

static LLRegisterPanelClassWrapper<LLPanelGroupRoles> t_panel_group_roles("panel_group_roles");

bool agentCanRemoveFromRole(const LLUUID& group_id,
							const LLUUID& role_id)
{
	return gAgent.hasPowerInGroup(group_id, GP_ROLE_REMOVE_MEMBER);
}

bool agentCanAddToRole(const LLUUID& group_id,
					   const LLUUID& role_id)
{
	if (gAgent.isGodlike())
		return true;
    
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(group_id);
	if (!gdatap) 
	{
		llwarns << "agentCanAddToRole "
				<< "-- No group data!" << llendl;
		return false;
	}

	//make sure the agent is in the group
	LLGroupMgrGroupData::member_list_t::iterator mi = gdatap->mMembers.find(gAgent.getID());
	if (mi == gdatap->mMembers.end())
	{
		return false;
	}
	
	LLGroupMemberData* member_data = (*mi).second;

	// Owners can add to any role.
	if ( member_data->isInRole(gdatap->mOwnerRole) )
	{
		return true;
	}

	// 'Limited assign members' can add to roles the user is in.
	if ( gAgent.hasPowerInGroup(group_id, GP_ROLE_ASSIGN_MEMBER_LIMITED) &&
			member_data->isInRole(role_id) )
	{
		return true;
	}

	// 'assign members' can add to non-owner roles.
	if ( gAgent.hasPowerInGroup(group_id, GP_ROLE_ASSIGN_MEMBER) &&
			 role_id != gdatap->mOwnerRole )
	{
		return true;
	}

	return false;
}


// LLPanelGroupRoles /////////////////////////////////////////////////////

// static
LLPanelGroupRoles::LLPanelGroupRoles()
:	LLPanelGroupTab(),
	mCurrentTab(NULL),
	mRequestedTab( NULL ),
	mSubTabContainer( NULL ),
	mFirstUse( TRUE )
{
}

LLPanelGroupRoles::~LLPanelGroupRoles()
{
}

BOOL LLPanelGroupRoles::postBuild()
{
	lldebugs << "LLPanelGroupRoles::postBuild()" << llendl;

	mSubTabContainer = getChild<LLTabContainer>("roles_tab_container");

	if (!mSubTabContainer) return FALSE;

	// Hook up each sub-tabs callback and widgets.
	for (S32 i = 0; i < mSubTabContainer->getTabCount(); ++i)
	{
		LLPanel* panel = mSubTabContainer->getPanelByIndex(i);
		LLPanelGroupSubTab* subtabp = dynamic_cast<LLPanelGroupSubTab*>(panel);
		if (!subtabp)
		{
			llwarns << "Invalid subtab panel: " << panel->getName() << llendl;
			return FALSE;
		}

		// Hand the subtab a pointer to this LLPanelGroupRoles, so that it can
		// look around for the widgets it is interested in.
		if (!subtabp->postBuildSubTab(this))
			return FALSE;

		//subtabp->addObserver(this);
	}
	// Add click callbacks to tab switching.
	mSubTabContainer->setValidateBeforeCommit(boost::bind(&LLPanelGroupRoles::handleSubTabSwitch, this, _1));

	// Set the current tab to whatever is currently being shown.
	mCurrentTab = (LLPanelGroupTab*) mSubTabContainer->getCurrentPanel();
	if (!mCurrentTab)
	{
		// Need to select a tab.
		mSubTabContainer->selectFirstTab();
		mCurrentTab = (LLPanelGroupTab*) mSubTabContainer->getCurrentPanel();
	}

	if (!mCurrentTab) return FALSE;

	// Act as though this tab was just activated.
	mCurrentTab->activate();

	// Read apply text from the xml file.
	mDefaultNeedsApplyMesg = getString("default_needs_apply_text");
	mWantApplyMesg = getString("want_apply_text");

	return LLPanelGroupTab::postBuild();
}

BOOL LLPanelGroupRoles::isVisibleByAgent(LLAgent* agentp)
{
	/* This power was removed to make group roles simpler
	return agentp->hasPowerInGroup(mGroupID, 
								   GP_ROLE_CREATE |
								   GP_ROLE_DELETE |
								   GP_ROLE_PROPERTIES |
								   GP_ROLE_VIEW |
								   GP_ROLE_ASSIGN_MEMBER |
								   GP_ROLE_REMOVE_MEMBER |
								   GP_ROLE_CHANGE_ACTIONS |
								   GP_MEMBER_INVITE |
								   GP_MEMBER_EJECT |
								   GP_MEMBER_OPTIONS );
	*/
	return mAllowEdit && agentp->isInGroup(mGroupID);
								   
}

bool LLPanelGroupRoles::handleSubTabSwitch(const LLSD& data)
{
	std::string panel_name = data.asString();
	
	if(mRequestedTab != NULL)//we already have tab change request
	{
		return false;
	}

	mRequestedTab = static_cast<LLPanelGroupTab*>(mSubTabContainer->getPanelByName(panel_name));

	std::string mesg;
	if (mCurrentTab && mCurrentTab->needsApply(mesg))
	{
		// If no message was provided, give a generic one.
		if (mesg.empty())
		{
			mesg = mDefaultNeedsApplyMesg;
		}
		// Create a notify box, telling the user about the unapplied tab.
		LLSD args;
		args["NEEDS_APPLY_MESSAGE"] = mesg;
		args["WANT_APPLY_MESSAGE"] = mWantApplyMesg;
		LLNotificationsUtil::add("PanelGroupApply", args, LLSD(),
			boost::bind(&LLPanelGroupRoles::handleNotifyCallback, this, _1, _2));
		mHasModal = TRUE;
		
		// Returning FALSE will block a close action from finishing until
		// we get a response back from the user.
		return false;
	}

	transitionToTab();
	return true;
}

void LLPanelGroupRoles::transitionToTab()
{
	// Tell the current panel that it is being deactivated.
	if (mCurrentTab)
	{
		mCurrentTab->deactivate();
	}
	
	// Tell the new panel that it is being activated.
	if (mRequestedTab)
	{
		// This is now the current tab;
		mCurrentTab = mRequestedTab;
		mCurrentTab->activate();
		mRequestedTab = 0;
	}
}

bool LLPanelGroupRoles::handleNotifyCallback(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
	mHasModal = FALSE;
	LLPanelGroupTab* transition_tab = mRequestedTab;
	switch (option)
	{
	case 0: // "Apply Changes"
	{
		// Try to apply changes, and switch to the requested tab.
		std::string apply_mesg;
		if ( !apply( apply_mesg ) )
		{
			// There was a problem doing the apply.
			if ( !apply_mesg.empty() )
			{
				mHasModal = TRUE;
				LLSD args;
				args["MESSAGE"] = apply_mesg;
				LLNotificationsUtil::add("GenericAlert", args, LLSD(), boost::bind(&LLPanelGroupRoles::onModalClose, this, _1, _2));
			}
			// Skip switching tabs.
			break;
		}
		transitionToTab();
		mSubTabContainer->selectTabPanel( transition_tab );
		
		break;
	}
	case 1: // "Ignore Changes"
		// Switch to the requested panel without applying changes
		cancel();
		transitionToTab();
		mSubTabContainer->selectTabPanel( transition_tab );
		break;
	case 2: // "Cancel"
	default:
		mRequestedTab = NULL;
		// Do nothing.  The user is canceling the action.
		break;
	}
	return false;
}

bool LLPanelGroupRoles::onModalClose(const LLSD& notification, const LLSD& response)
{
	mHasModal = FALSE;
	return false;
}

bool LLPanelGroupRoles::apply(std::string& mesg)
{
	// Pass this along to the currently visible sub tab.
	if (!mSubTabContainer) return false;

	LLPanelGroupTab* panelp = (LLPanelGroupTab*) mSubTabContainer->getCurrentPanel();
	if (!panelp) return false;
	
	// Ignore the needs apply message.
	std::string ignore_mesg;
	if ( !panelp->needsApply(ignore_mesg) )
	{
		// We don't need to apply anything.
		// We're done.
		return true;
	}

	// Try to do the actual apply.
	return panelp->apply(mesg);
}

void LLPanelGroupRoles::cancel()
{
	// Pass this along to the currently visible sub tab.
	if (!mSubTabContainer) return;

	LLPanelGroupTab* panelp = (LLPanelGroupTab*) mSubTabContainer->getCurrentPanel();
	if (!panelp) return;

	panelp->cancel();
}

void LLPanelGroupRoles::update(LLGroupChange gc)
{
	if (mGroupID.isNull()) return;
	
	LLPanelGroupTab* panelp = (LLPanelGroupTab*) mSubTabContainer->getCurrentPanel();
	if (panelp)
	{
		panelp->update(gc);
	}
	else
	{
		llwarns << "LLPanelGroupRoles::update() -- No subtab to update!" << llendl;
	}
	
}

void LLPanelGroupRoles::activate()
{
	// Start requesting member and role data if needed.
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap || !gdatap->isMemberDataComplete() )
	{
		LLGroupMgr::getInstance()->sendCapGroupMembersRequest(mGroupID);
	}

	if (!gdatap || !gdatap->isRoleDataComplete() )
	{
		// Mildly hackish - clear all pending changes
		cancel();

		LLGroupMgr::getInstance()->sendGroupRoleDataRequest(mGroupID);
	}

	// Check role-member mapping data.
	if (!gdatap || !gdatap->isRoleMemberDataComplete() )
	{
		LLGroupMgr::getInstance()->sendGroupRoleMembersRequest(mGroupID);
	}

	// Need this to get base group member powers
	if (!gdatap || !gdatap->isGroupPropertiesDataComplete() )
	{
		LLGroupMgr::getInstance()->sendGroupPropertiesRequest(mGroupID);
	}

	mFirstUse = FALSE;

	LLPanelGroupTab* panelp = (LLPanelGroupTab*) mSubTabContainer->getCurrentPanel();
	if (panelp) panelp->activate();
}

void LLPanelGroupRoles::deactivate()
{
	LLPanelGroupTab* panelp = (LLPanelGroupTab*) mSubTabContainer->getCurrentPanel();
	if (panelp) panelp->deactivate();
}

bool LLPanelGroupRoles::needsApply(std::string& mesg)
{
	LLPanelGroupTab* panelp = (LLPanelGroupTab*) mSubTabContainer->getCurrentPanel();
	if (!panelp) return false;
		
	return panelp->needsApply(mesg);
}

BOOL LLPanelGroupRoles::hasModal()
{
	if (mHasModal) return TRUE;

	LLPanelGroupTab* panelp = (LLPanelGroupTab*) mSubTabContainer->getCurrentPanel();
	if (!panelp) return FALSE;
		
	return panelp->hasModal();
}

void LLPanelGroupRoles::setGroupID(const LLUUID& id)
{
	LLPanelGroupTab::setGroupID(id);

	LLPanelGroupMembersSubTab* group_members_tab = findChild<LLPanelGroupMembersSubTab>("members_sub_tab");
	LLPanelGroupRolesSubTab*  group_roles_tab = findChild<LLPanelGroupRolesSubTab>("roles_sub_tab");
	LLPanelGroupActionsSubTab* group_actions_tab = findChild<LLPanelGroupActionsSubTab>("actions_sub_tab");
	LLPanelGroupBanListSubTab* group_ban_tab = findChild<LLPanelGroupBanListSubTab>("banlist_sub_tab");

	if(group_members_tab) group_members_tab->setGroupID(id);
	if(group_roles_tab) group_roles_tab->setGroupID(id);
	if(group_actions_tab) group_actions_tab->setGroupID(id);
	if(group_ban_tab) group_ban_tab->setGroupID(id);

	LLButton* button = getChild<LLButton>("member_invite");
	if ( button )
		button->setEnabled(gAgent.hasPowerInGroup(mGroupID, GP_MEMBER_INVITE));

	if(mSubTabContainer)
		mSubTabContainer->selectTab(0);

	activate();
}


// LLPanelGroupSubTab ////////////////////////////////////////////////////
LLPanelGroupSubTab::LLPanelGroupSubTab()
:	LLPanelGroupTab(),
	mHeader(NULL),
	mFooter(NULL),
	mActivated(false),
	mSearchEditor(NULL)
{
}

LLPanelGroupSubTab::~LLPanelGroupSubTab()
{
}

BOOL LLPanelGroupSubTab::postBuildSubTab(LLView* root) 
{ 
	// Get icons for later use.
	mActionIcons.clear();

	if (hasString("power_folder_icon"))
	{
		mActionIcons["folder"] = getString("power_folder_icon");
	}

	if (hasString("power_all_have_icon"))
	{
		mActionIcons["full"] = getString("power_all_have_icon");
	}

	if (hasString("power_partial_icon"))
	{
		mActionIcons["partial"] = getString("power_partial_icon");
	}
	return TRUE; 
}

BOOL LLPanelGroupSubTab::postBuild()
{
	// Hook up the search widgets.
	bool recurse = true;
	mSearchEditor = getChild<LLFilterEditor>("filter_input", recurse);

	if (!mSearchEditor) 
		return FALSE;

	mSearchEditor->setCommitCallback(boost::bind(&LLPanelGroupSubTab::setSearchFilter, this, _2));

	return LLPanelGroupTab::postBuild();
}

void LLPanelGroupSubTab::setGroupID(const LLUUID& id)
{
	LLPanelGroupTab::setGroupID(id);
	if(mSearchEditor)
	{
		mSearchEditor->clear();
		setSearchFilter("");
	}

	mActivated = false;
}

void LLPanelGroupSubTab::setSearchFilter(const std::string& filter)
{
	if(mSearchFilter == filter)
		return;
	mSearchFilter = filter;
	LLStringUtil::toLower(mSearchFilter);
	update(GC_ALL);
}

void LLPanelGroupSubTab::activate()
{
	setOthersVisible(TRUE);
}

void LLPanelGroupSubTab::deactivate()
{
	setOthersVisible(FALSE);
}

void LLPanelGroupSubTab::setOthersVisible(BOOL b)
{
	if (mHeader)
	{
		mHeader->setVisible( b );
	}

	if (mFooter)
	{
		mFooter->setVisible( b );
	}
}

bool LLPanelGroupSubTab::matchesActionSearchFilter(std::string action)
{
	// If the search filter is empty, everything passes.
	if (mSearchFilter.empty()) return true;

	LLStringUtil::toLower(action);
	std::string::size_type match = action.find(mSearchFilter);

	if (std::string::npos == match)
	{
		// not found
		return false;
	}
	else
	{
		return true;
	}
}

void LLPanelGroupSubTab::buildActionsList(LLScrollListCtrl* ctrl, 
										  U64 allowed_by_some, 
										  U64 allowed_by_all,
										  LLUICtrl::commit_callback_t commit_callback,
										  BOOL show_all,
										  BOOL filter,
										  BOOL is_owner_role)
{
	if (LLGroupMgr::getInstance()->mRoleActionSets.empty())
	{
		llwarns << "Can't build action list - no actions found." << llendl;
		return;
	}

	std::vector<LLRoleActionSet*>::iterator ras_it = LLGroupMgr::getInstance()->mRoleActionSets.begin();
	std::vector<LLRoleActionSet*>::iterator ras_end = LLGroupMgr::getInstance()->mRoleActionSets.end();

	for ( ; ras_it != ras_end; ++ras_it)
	{
		buildActionCategory(ctrl,
							allowed_by_some,
							allowed_by_all,
							(*ras_it),
							commit_callback,
							show_all,
							filter,
							is_owner_role);
	}
}

void LLPanelGroupSubTab::buildActionCategory(LLScrollListCtrl* ctrl,
											 U64 allowed_by_some,
											 U64 allowed_by_all,
											 LLRoleActionSet* action_set,
											 LLUICtrl::commit_callback_t commit_callback,
											 BOOL show_all,
											 BOOL filter,
											 BOOL is_owner_role)
{
	lldebugs << "Building role list for: " << action_set->mActionSetData->mName << llendl;
	// See if the allow mask matches anything in this category.
	if (show_all || (allowed_by_some & action_set->mActionSetData->mPowerBit))
	{
		// List all the actions in this category that at least some members have.
		LLSD row;

		row["columns"][0]["column"] = "icon";
		row["columns"][0]["type"] = "icon";
		
		icon_map_t::iterator iter = mActionIcons.find("folder");
		if (iter != mActionIcons.end())
		{
			row["columns"][0]["value"] = (*iter).second;
		}

		row["columns"][1]["column"] = "action";
		row["columns"][1]["type"] = "text";
		row["columns"][1]["value"] = LLTrans::getString(action_set->mActionSetData->mName);
		row["columns"][1]["font"]["name"] = "SANSSERIF_SMALL";
		

		LLScrollListItem* title_row = ctrl->addElement(row, ADD_BOTTOM, action_set->mActionSetData);
		
		LLScrollListText* name_textp = dynamic_cast<LLScrollListText*>(title_row->getColumn(2)); //?? I have no idea fix getColumn(1) return column spacer...
		if (name_textp)
			name_textp->setFontStyle(LLFontGL::BOLD);

		bool category_matches_filter = (filter) ? matchesActionSearchFilter(action_set->mActionSetData->mName) : true;

		std::vector<LLRoleAction*>::iterator ra_it = action_set->mActions.begin();
		std::vector<LLRoleAction*>::iterator ra_end = action_set->mActions.end();

		bool items_match_filter = false;
		BOOL can_change_actions = (!is_owner_role && gAgent.hasPowerInGroup(mGroupID, GP_ROLE_CHANGE_ACTIONS));

		for ( ; ra_it != ra_end; ++ra_it)
		{
			// See if anyone has these action.
			if (!show_all && !(allowed_by_some & (*ra_it)->mPowerBit))
			{
				continue;
			}

			// See if we are filtering out these actions
			// If we aren't using filters, category_matches_filter will be true.
			if (!category_matches_filter
				&& !matchesActionSearchFilter((*ra_it)->mDescription))
			{
				continue;										
			}

			items_match_filter = true;

			// See if everyone has these actions.
			bool show_full_strength = false;
			if ( (allowed_by_some & (*ra_it)->mPowerBit) == (allowed_by_all & (*ra_it)->mPowerBit) )
			{
				show_full_strength = true;
			}

			LLSD row;

			S32 column_index = 0;
			row["columns"][column_index]["column"] = "icon";
			++column_index;

			
			S32 check_box_index = -1;
			if (commit_callback)
			{
				row["columns"][column_index]["column"] = "checkbox";
				row["columns"][column_index]["type"] = "checkbox";
				check_box_index = column_index;
				++column_index;
			}
			else
			{
				if (show_full_strength)
				{
					icon_map_t::iterator iter = mActionIcons.find("full");
					if (iter != mActionIcons.end())
					{
						row["columns"][column_index]["column"] = "checkbox";
						row["columns"][column_index]["type"] = "icon";
						row["columns"][column_index]["value"] = (*iter).second;
						++column_index;
					}
				}
				else
				{
					icon_map_t::iterator iter = mActionIcons.find("partial");
					if (iter != mActionIcons.end())
					{
						row["columns"][column_index]["column"] = "checkbox";
						row["columns"][column_index]["type"] = "icon";
						row["columns"][column_index]["value"] = (*iter).second;
						++column_index;
					}
					row["enabled"] = false;
				}
			}

			row["columns"][column_index]["column"] = "action";
			row["columns"][column_index]["value"] = (*ra_it)->mDescription;
			row["columns"][column_index]["font"] = "SANSSERIF_SMALL";

			LLScrollListItem* item = ctrl->addElement(row, ADD_BOTTOM, (*ra_it));

			if (-1 != check_box_index)
			{
				// Extract the checkbox that was created.
				LLScrollListCheck* check_cell = (LLScrollListCheck*) item->getColumn(check_box_index);
				LLCheckBoxCtrl* check = check_cell->getCheckBox();
				check->setEnabled(can_change_actions);
				check->setCommitCallback(commit_callback);
				check->setToolTip( check->getLabel() );

				if (show_all)
				{
					check->setTentative(FALSE);
					if (allowed_by_some & (*ra_it)->mPowerBit)
					{
						check->set(TRUE);
					}
					else
					{
						check->set(FALSE);
					}
				}
				else
				{
					check->set(TRUE);
					if (show_full_strength)
					{
						check->setTentative(FALSE);
					}
					else
					{
						check->setTentative(TRUE);
					}
				}
			}
		}

		if (!items_match_filter)
		{
			S32 title_index = ctrl->getItemIndex(title_row);
			ctrl->deleteSingleItem(title_index);
		}
	}
}

void LLPanelGroupSubTab::setFooterEnabled(BOOL enable)
{
	if (mFooter)
	{
		mFooter->setAllChildrenEnabled(enable);
	}
}


// LLPanelGroupMembersSubTab /////////////////////////////////////////////
static LLRegisterPanelClassWrapper<LLPanelGroupMembersSubTab> t_panel_group_members_subtab("panel_group_members_subtab");

LLPanelGroupMembersSubTab::LLPanelGroupMembersSubTab()
: 	LLPanelGroupSubTab(),
	mMembersList(NULL),
	mAssignedRolesList(NULL),
	mAllowedActionsList(NULL),
	mChanged(FALSE),
	mPendingMemberUpdate(FALSE),
	mHasMatch(FALSE),
	mNumOwnerAdditions(0),
	mAvatarNameCacheConnection()
{
}

LLPanelGroupMembersSubTab::~LLPanelGroupMembersSubTab()
{
	if (mAvatarNameCacheConnection.connected())
	{
		mAvatarNameCacheConnection.disconnect();
	}
	if (mMembersList)
	{
		gSavedSettings.setString("GroupMembersSortOrder", mMembersList->getSortColumnName());
	}
}

BOOL LLPanelGroupMembersSubTab::postBuildSubTab(LLView* root)
{
	LLPanelGroupSubTab::postBuildSubTab(root);
	
	// Upcast parent so we can ask it for sibling controls.
	LLPanelGroupRoles* parent = (LLPanelGroupRoles*) root;

	// Look recursively from the parent to find all our widgets.
	bool recurse = true;
	mHeader = parent->getChild<LLPanel>("members_header", recurse);
	mFooter = parent->getChild<LLPanel>("members_footer", recurse);

	mMembersList 		= parent->getChild<LLNameListCtrl>("member_list", recurse);
	mAssignedRolesList	= parent->getChild<LLScrollListCtrl>("member_assigned_roles", recurse);
	mAllowedActionsList	= parent->getChild<LLScrollListCtrl>("member_allowed_actions", recurse);

	if (!mMembersList || !mAssignedRolesList || !mAllowedActionsList) return FALSE;

	// We want to be notified whenever a member is selected.
	mMembersList->setCommitOnSelectionChange(TRUE);
	mMembersList->setCommitCallback(onMemberSelect, this);
	// Show the member's profile on double click.
	mMembersList->setDoubleClickCallback(onMemberDoubleClick, this);
	mMembersList->setContextMenu(LLScrollListCtrl::MENU_AVATAR);
	
	LLSD row;
	row["columns"][0]["column"] = "name";
	row["columns"][1]["column"] = "donated";
	row["columns"][2]["column"] = "online";
	mMembersList->addElement(row);
	std::string order_by = gSavedSettings.getString("GroupMembersSortOrder");
	if(!order_by.empty())
	{
		mMembersList->sortByColumn(order_by, TRUE);
	}	

	LLButton* button = parent->getChild<LLButton>("member_invite", recurse);
	if ( button )
	{
		button->setClickedCallback(onInviteMember, this);
		button->setEnabled(gAgent.hasPowerInGroup(mGroupID, GP_MEMBER_INVITE));
	}

	mEjectBtn = parent->getChild<LLButton>("member_eject", recurse);
	if ( mEjectBtn )
	{
		mEjectBtn->setClickedCallback(onEjectMembers, this);
		mEjectBtn->setEnabled(FALSE);
	}

	mBanBtn = parent->getChild<LLButton>("member_ban", recurse);
	if(mBanBtn)
	{
		mBanBtn->setClickedCallback(onBanMember, this);
		mBanBtn->setEnabled(FALSE);
	}

	return TRUE;
}

void LLPanelGroupMembersSubTab::setGroupID(const LLUUID& id)
{
	//clear members list
	if(mMembersList) mMembersList->deleteAllItems();
	if(mAssignedRolesList) mAssignedRolesList->deleteAllItems();
	if(mAllowedActionsList) mAllowedActionsList->deleteAllItems();

	LLPanelGroupSubTab::setGroupID(id);
}

// static
void LLPanelGroupMembersSubTab::onMemberSelect(LLUICtrl* ctrl, void* user_data)
{
	LLPanelGroupMembersSubTab* self = static_cast<LLPanelGroupMembersSubTab*>(user_data);
	self->handleMemberSelect();
}

void LLPanelGroupMembersSubTab::handleMemberSelect()
{
	lldebugs << "LLPanelGroupMembersSubTab::handleMemberSelect" << llendl;

	mAssignedRolesList->deleteAllItems();
	mAllowedActionsList->deleteAllItems();
	
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap) 
	{
		llwarns << "LLPanelGroupMembersSubTab::handleMemberSelect() "
				<< "-- No group data!" << llendl;
		return;
	}

	// Check if there is anything selected.
	std::vector<LLScrollListItem*> selection = mMembersList->getAllSelected();
	if (selection.empty()) return;

	// Build a vector of all selected members, and gather allowed actions.
	uuid_vec_t selected_members;
	U64 allowed_by_all = 0xffffffffffffLL;
	U64 allowed_by_some = 0;

	std::vector<LLScrollListItem*>::iterator itor;
	for (itor = selection.begin();
		 itor != selection.end(); ++itor)
	{
		LLUUID member_id = (*itor)->getUUID();

		selected_members.push_back( member_id );
		// Get this member's power mask including any unsaved changes

		U64 powers = getAgentPowersBasedOnRoleChanges( member_id );

		allowed_by_all &= powers;
		allowed_by_some |= powers;
	}
	std::sort(selected_members.begin(), selected_members.end());

	//////////////////////////////////
	// Build the allowed actions list.
	//////////////////////////////////
	buildActionsList(mAllowedActionsList,
					 allowed_by_some,
					 allowed_by_all,
					 NULL,
					 FALSE,
					 FALSE,
					 FALSE);

	//////////////////////////////////
	// Build the assigned roles list.
	//////////////////////////////////
	// Add each role to the assigned roles list.
	LLGroupMgrGroupData::role_list_t::iterator iter = gdatap->mRoles.begin();
	LLGroupMgrGroupData::role_list_t::iterator end  = gdatap->mRoles.end();

	BOOL can_ban_members = gAgent.hasPowerInGroup(mGroupID,	GP_GROUP_BAN_ACCESS);
	BOOL can_eject_members = gAgent.hasPowerInGroup(mGroupID, GP_MEMBER_EJECT);
	BOOL member_is_owner = FALSE;
	
	for( ; iter != end; ++iter)
	{
		// Count how many selected users are in this role.
		const LLUUID& role_id = iter->first;
		LLGroupRoleData* group_role_data = iter->second;

		if (group_role_data)
		{
			const BOOL needs_sort = FALSE;
			S32 count = group_role_data->getMembersInRole(
											selected_members, needs_sort);
			//check if the user has permissions to assign/remove
			//members to/from the role (but the ability to add/remove
			//should only be based on the "saved" changes to the role
			//not in the temp/meta data. -jwolk
			BOOL cb_enable = ( (count > 0) ?
							   agentCanRemoveFromRole(mGroupID, role_id) :
							   agentCanAddToRole(mGroupID, role_id) );


			// Owner role has special enabling permissions for removal.
			if (cb_enable && (count > 0) && role_id == gdatap->mOwnerRole)
			{
				// Check if any owners besides this agent are selected.
				uuid_vec_t::const_iterator member_iter;
				uuid_vec_t::const_iterator member_end =
												selected_members.end();
				for (member_iter = selected_members.begin();
					 member_iter != member_end;	
					 ++member_iter)
				{
					// Don't count the agent.
					if ((*member_iter) == gAgent.getID()) continue;
					
					// Look up the member data.
					LLGroupMgrGroupData::member_list_t::iterator mi = 
									gdatap->mMembers.find((*member_iter));
					if (mi == gdatap->mMembers.end()) continue;
					LLGroupMemberData* member_data = (*mi).second;
					// Is the member an owner?
					if ( member_data && member_data->isInRole(gdatap->mOwnerRole) )
					{
						// Can't remove other owners.
						cb_enable = FALSE;
						break;
					}
				}
			}

			//now see if there are any role changes for the selected
			//members and remember to include them
			uuid_vec_t::iterator sel_mem_iter = selected_members.begin();
			for (; sel_mem_iter != selected_members.end(); sel_mem_iter++)
			{
				LLRoleMemberChangeType type;
				if ( getRoleChangeType(*sel_mem_iter, role_id, type) )
				{
					if ( type == RMC_ADD ) count++;
					else if ( type == RMC_REMOVE ) count--;
				}
			}
			
			// If anyone selected is in any role besides 'Everyone' then they can't be ejected.
			if (role_id.notNull() && (count > 0))
			{
				can_eject_members = FALSE;
				can_ban_members = FALSE;
				if (role_id == gdatap->mOwnerRole)
				{
					member_is_owner = TRUE;
				}
			}

			LLRoleData rd;
			if (gdatap->getRoleData(role_id,rd))
			{
				std::ostringstream label;
				label << rd.mRoleName;
				// Don't bother showing a count, if there is only 0 or 1.
				if (count > 1)
				{
					label << ": " << count ;
				}
	
				LLSD row;
				row["id"] = role_id;

				row["columns"][0]["column"] = "checkbox";
				row["columns"][0]["type"] = "checkbox";

				row["columns"][1]["column"] = "role";
				row["columns"][1]["value"] = label.str();

				if (row["id"].asUUID().isNull())
				{
					// This is the everyone role, you can't take people out of the everyone role!
					row["enabled"] = false;
				}

				LLScrollListItem* item = mAssignedRolesList->addElement(row);

				// Extract the checkbox that was created.
				LLScrollListCheck* check_cell = (LLScrollListCheck*) item->getColumn(0);
				LLCheckBoxCtrl* check = check_cell->getCheckBox();
				check->setCommitCallback(onRoleCheck, this);
				check->set( count > 0 );
				check->setTentative(
					(0 != count)
					&& (selected_members.size() !=
						(uuid_vec_t::size_type)count));

				//NOTE: as of right now a user can break the group
				//by removing himself from a role if he is the
				//last owner.  We should check for this special case
				// -jwolk
				check->setEnabled(cb_enable);
				item->setEnabled(cb_enable);
			}
		}
		else
		{
			// This could happen if changes are not synced right on sub-panel change.
			llwarns << "No group role data for " << iter->second << llendl;
		}
	}
	mAssignedRolesList->setEnabled(TRUE);

	if (gAgent.isGodlike())
	{
		can_eject_members = TRUE;
		can_ban_members = TRUE;
	}

	if (!can_eject_members && !member_is_owner)
	{
		// Maybe we can eject them because we are an owner...
		LLGroupMgrGroupData::member_list_t::iterator mi = gdatap->mMembers.find(gAgent.getID());
		if (mi != gdatap->mMembers.end())
		{
			LLGroupMemberData* member_data = (*mi).second;

			if ( member_data && member_data->isInRole(gdatap->mOwnerRole) )
			{
				can_eject_members = TRUE;
				can_ban_members = TRUE;
			}
		}
	}

	mBanBtn->setEnabled(can_ban_members);
	mEjectBtn->setEnabled(can_eject_members);
}

// static
void LLPanelGroupMembersSubTab::onMemberDoubleClick(void* user_data)
{
	LLPanelGroupMembersSubTab* self = static_cast<LLPanelGroupMembersSubTab*>(user_data);
	self->handleMemberDoubleClick();
}

//static
void LLPanelGroupMembersSubTab::onInviteMember(void *userdata)
{
	LLPanelGroupMembersSubTab* selfp = (LLPanelGroupMembersSubTab*) userdata;

	if ( selfp )
	{
		selfp->handleInviteMember();
	}
}

void LLPanelGroupMembersSubTab::handleInviteMember()
{
	LLFloaterGroupInvite::showForGroup(mGroupID);
}

void LLPanelGroupMembersSubTab::onEjectMembers(void *userdata)
{
	LLPanelGroupMembersSubTab* selfp = (LLPanelGroupMembersSubTab*) userdata;

	if ( selfp )
	{
		selfp->handleEjectMembers();
	}
}

void LLPanelGroupMembersSubTab::handleEjectMembers()
{
	//send down an eject message
	uuid_vec_t selected_members;

	std::vector<LLScrollListItem*> selection = mMembersList->getAllSelected();
	if (selection.empty()) return;

	std::vector<LLScrollListItem*>::iterator itor;
	for (itor = selection.begin() ; 
		 itor != selection.end(); ++itor)
	{
		LLUUID member_id = (*itor)->getUUID();
		selected_members.push_back( member_id );
	}

	mMembersList->deleteSelectedItems();

	sendEjectNotifications(mGroupID, selected_members);

	LLGroupMgr::getInstance()->sendGroupMemberEjects(mGroupID, selected_members);
}

void LLPanelGroupMembersSubTab::sendEjectNotifications(const LLUUID& group_id, const uuid_vec_t& selected_members)
{
	LLGroupMgrGroupData* group_data = LLGroupMgr::getInstance()->getGroupData(group_id);

	if (group_data)
	{
		for (uuid_vec_t::const_iterator i = selected_members.begin(); i != selected_members.end(); ++i)
		{
			LLSD args;
			args["AVATAR_NAME"] = LLSLURL("agent", *i, "displayname").getSLURLString();
			args["GROUP_NAME"] = group_data->mName;
			
			LLNotifications::instance().add(LLNotification::Params("EjectAvatarFromGroup").substitutions(args));
		}
	}
}

void LLPanelGroupMembersSubTab::handleRoleCheck(const LLUUID& role_id,
												LLRoleMemberChangeType type)
{
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap) return;

	//add that the user is requesting to change the roles for selected
	//members
	U64 powers_all_have  = 0xffffffffffffLL;
	U64 powers_some_have = 0;

	BOOL   is_owner_role = ( gdatap->mOwnerRole == role_id );
	LLUUID member_id;

	std::vector<LLScrollListItem*> selection = mMembersList->getAllSelected();
	if (selection.empty())
	{
		return;
	}
	
	for (std::vector<LLScrollListItem*>::iterator itor = selection.begin() ; 
		 itor != selection.end(); ++itor)
	{
		member_id = (*itor)->getUUID();

		//see if we requested a change for this member before
		if ( mMemberRoleChangeData.find(member_id) == mMemberRoleChangeData.end() )
		{
			mMemberRoleChangeData[member_id] = new role_change_data_map_t;
		}
		role_change_data_map_t* role_change_datap = mMemberRoleChangeData[member_id];

		//now check to see if the selected group member
		//had changed his association with the selected role before

		role_change_data_map_t::iterator  role = role_change_datap->find(role_id);
		if ( role != role_change_datap->end() )
		{
			//see if the new change type cancels out the previous change
			if (role->second != type)
			{
				role_change_datap->erase(role_id);
				if ( is_owner_role ) mNumOwnerAdditions--;
			}
			//else do nothing

			if ( role_change_datap->empty() )
			{
				//the current member now has no role changes
				//so erase the role change and erase the member's entry
				delete role_change_datap;
                role_change_datap = NULL;

				mMemberRoleChangeData.erase(member_id);
			}
		}
		else
		{
			//a previously unchanged role is being changed
			(*role_change_datap)[role_id] = type;
			if ( is_owner_role && type == RMC_ADD ) mNumOwnerAdditions++;
		}

		//we need to calculate what powers the selected members
		//have (including the role changes we're making)
		//so that we can rebuild the action list
		U64 new_powers = getAgentPowersBasedOnRoleChanges(member_id);

		powers_all_have  &= new_powers;
		powers_some_have |= new_powers;
	}

	
	mChanged = !mMemberRoleChangeData.empty();
	notifyObservers();

	//alrighty now we need to update the actions list
	//to reflect the changes
	mAllowedActionsList->deleteAllItems();
	buildActionsList(mAllowedActionsList,
					 powers_some_have,
					 powers_all_have,
					 NULL,
					 FALSE,
					 FALSE,
					 FALSE);
}

// static 
void LLPanelGroupMembersSubTab::onRoleCheck(LLUICtrl* ctrl, void* user_data)
{
	LLPanelGroupMembersSubTab* self = static_cast<LLPanelGroupMembersSubTab*>(user_data);
	LLCheckBoxCtrl* check_box = static_cast<LLCheckBoxCtrl*>(ctrl);
	if (!check_box || !self) return;

	LLScrollListItem* first_selected =
		self->mAssignedRolesList->getFirstSelected();
	if (first_selected)
	{
		LLUUID role_id = first_selected->getUUID();
		LLRoleMemberChangeType change_type = (check_box->get() ? 
						      RMC_ADD : 
						      RMC_REMOVE);
		
		self->handleRoleCheck(role_id, change_type);
	}
}

void LLPanelGroupMembersSubTab::handleMemberDoubleClick()
{
	LLScrollListItem* selected = mMembersList->getFirstSelected();
	if (selected)
	{
		LLUUID member_id = selected->getUUID();
		LLAvatarActions::showProfile( member_id );
	}
}

void LLPanelGroupMembersSubTab::activate()
{
	LLPanelGroupSubTab::activate();
	if(!mActivated)
	{
		update(GC_ALL);
		mActivated = true;
	}
}

void LLPanelGroupMembersSubTab::deactivate()
{
	LLPanelGroupSubTab::deactivate();
}

bool LLPanelGroupMembersSubTab::needsApply(std::string& mesg)
{
	return mChanged;
}

void LLPanelGroupMembersSubTab::cancel()
{
	if ( mChanged )
	{
		std::for_each(mMemberRoleChangeData.begin(),
					  mMemberRoleChangeData.end(),
					  DeletePairedPointer());
		mMemberRoleChangeData.clear();

		mChanged = FALSE;
		notifyObservers();
	}
}

bool LLPanelGroupMembersSubTab::apply(std::string& mesg)
{
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap)
	{
		llwarns << "Unable to get group data for group " << mGroupID << llendl;

		mesg.assign("Unable to save member data.  Try again later.");
		return false;
	}

	if (mChanged)
	{
		//figure out if we are somehow adding an owner or not and alert
		//the user...possibly make it ignorable
		if ( mNumOwnerAdditions > 0 )
		{
			LLRoleData rd;
			LLSD args;

			if ( gdatap->getRoleData(gdatap->mOwnerRole, rd) )
			{
				mHasModal = TRUE;
				args["ROLE_NAME"] = rd.mRoleName;
				LLNotificationsUtil::add("AddGroupOwnerWarning",
										args,
										LLSD(),
										boost::bind(&LLPanelGroupMembersSubTab::addOwnerCB, this, _1, _2));
			}
			else
			{
				llwarns << "Unable to get role information for the owner role in group " << mGroupID << llendl;

				mesg.assign("Unable to retried specific group information.  Try again later");
				return false;
			}
				 
		}
		else
		{
			applyMemberChanges();
		}
	}

	return true;
}

bool LLPanelGroupMembersSubTab::addOwnerCB(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
	mHasModal = FALSE;

	if (0 == option)
	{
		// User clicked "Yes"
		applyMemberChanges();
	}
	return false;
}

void LLPanelGroupMembersSubTab::applyMemberChanges()
{
	//sucks to do a find again here, but it is in constant time, so, could
	//be worse
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap)
	{
		llwarns << "Unable to get group data for group " << mGroupID << llendl;
		return;
	}

	//we need to add all of the changed roles data
	//for each member whose role changed
	for (member_role_changes_map_t::iterator member = mMemberRoleChangeData.begin();
		 member != mMemberRoleChangeData.end(); ++member)
	{
		for (role_change_data_map_t::iterator role = member->second->begin();
			 role != member->second->end(); ++role)
		{
			gdatap->changeRoleMember(role->first, //role_id
									 member->first, //member_id
									 role->second); //add/remove
		}

		member->second->clear();
		delete member->second;
	}
	mMemberRoleChangeData.clear();

	LLGroupMgr::getInstance()->sendGroupRoleMemberChanges(mGroupID);	
	//force a UI update
	handleMemberSelect();

	mChanged = FALSE;
	mNumOwnerAdditions = 0;
	notifyObservers();
}

bool LLPanelGroupMembersSubTab::matchesSearchFilter(const std::string& fullname)
{
	// If the search filter is empty, everything passes.
	if (mSearchFilter.empty()) return true;

	// Create a full name, and compare it to the search filter.
	std::string fullname_lc(fullname);
	LLStringUtil::toLower(fullname_lc);

	std::string::size_type match = fullname_lc.find(mSearchFilter);

	if (std::string::npos == match)
	{
		// not found
		return false;
	}
	else
	{
		return true;
	}
}

U64 LLPanelGroupMembersSubTab::getAgentPowersBasedOnRoleChanges(const LLUUID& agent_id)
{
	//we loop over all of the changes
	//if we are adding a role, then we simply add the role's powers
	//if we are removing a role, we store that role id away
	//and then we have to build the powers up bases on the roles the agent
	//is in

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap) 
	{
		llwarns << "LLPanelGroupMembersSubTab::getAgentPowersBasedOnRoleChanges() -- No group data!" << llendl;
		return GP_NO_POWERS;
	}

	LLGroupMgrGroupData::member_list_t::iterator iter = gdatap->mMembers.find(agent_id);
	if ( iter == gdatap->mMembers.end() )
	{
		llwarns << "LLPanelGroupMembersSubTab::getAgentPowersBasedOnRoleChanges() -- No member data for member with UUID " << agent_id << llendl;
		return GP_NO_POWERS;
	}

	LLGroupMemberData* member_data = (*iter).second;
	if (!member_data)
	{
		llwarns << "LLPanelGroupMembersSubTab::getAgentPowersBasedOnRoleChanges() -- Null member data for member with UUID " << agent_id << llendl;
		return GP_NO_POWERS;
	}

	//see if there are unsaved role changes for this agent
	role_change_data_map_t* role_change_datap = NULL;
	member_role_changes_map_t::iterator member = mMemberRoleChangeData.find(agent_id);
	if ( member != mMemberRoleChangeData.end() )
	{
		//this member has unsaved role changes
		//so grab them
		role_change_datap = (*member).second;
	}

	U64 new_powers = GP_NO_POWERS;

	if ( role_change_datap )
	{
		uuid_vec_t roles_to_be_removed;

		for (role_change_data_map_t::iterator role = role_change_datap->begin();
			 role != role_change_datap->end(); ++ role)
		{
			if ( role->second == RMC_ADD )
			{
				new_powers |= gdatap->getRolePowers(role->first);
			}
			else
			{
				roles_to_be_removed.push_back(role->first);
			}
		}

		//loop over the member's current roles, summing up
		//the powers (not including the role we are removing)
		for (LLGroupMemberData::role_list_t::iterator current_role = member_data->roleBegin();
			 current_role != member_data->roleEnd(); ++current_role)
		{
			bool role_in_remove_list =
				(std::find(roles_to_be_removed.begin(),
						   roles_to_be_removed.end(),
						   current_role->second->getID()) !=
				 roles_to_be_removed.end());

			if ( !role_in_remove_list )
			{
				new_powers |= 
					current_role->second->getRoleData().mRolePowers;
			}
		}
	}
	else
	{
		//there are no changes for this member
		//the member's powers are just the ones stored in the group
		//manager
		new_powers = member_data->getAgentPowers();
	}

	return new_powers;
}

//If there is no change, returns false be sure to verify
//that there is a role change before attempting to get it or else
//the data will make no sense.  Stores the role change type
bool LLPanelGroupMembersSubTab::getRoleChangeType(const LLUUID& member_id,
												  const LLUUID& role_id,
												  LLRoleMemberChangeType& type)
{
	member_role_changes_map_t::iterator member_changes_iter = mMemberRoleChangeData.find(member_id);
	if ( member_changes_iter != mMemberRoleChangeData.end() )
	{
		role_change_data_map_t::iterator role_changes_iter = member_changes_iter->second->find(role_id);
		if ( role_changes_iter != member_changes_iter->second->end() )
		{
			type = role_changes_iter->second;
			return true;
		}
	}

	return false;
}

void LLPanelGroupMembersSubTab::draw()
{
	LLPanelGroupSubTab::draw();

	if (mPendingMemberUpdate)
	{
		updateMembers();
	}
}

void LLPanelGroupMembersSubTab::update(LLGroupChange gc)
{
	if (mGroupID.isNull()) return;

	if ( GC_TITLES == gc || GC_PROPERTIES == gc )
	{
		// Don't care about title or general group properties updates.
		return;
	}

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap) 
	{
		llwarns << "LLPanelGroupMembersSubTab::update() -- No group data!" << llendl;
		return;
	}

	// Wait for both all data to be retrieved before displaying anything.
	if (   gdatap->isMemberDataComplete() 
		&& gdatap->isRoleDataComplete()
		&& gdatap->isRoleMemberDataComplete())
	{
		mMemberProgress = gdatap->mMembers.begin();
		mPendingMemberUpdate = TRUE;
		mHasMatch = FALSE;
	}
	else
	{
		// Build a string with info on retrieval progress.
		std::ostringstream retrieved;
		if ( !gdatap->isMemberDataComplete() )
		{
			// Still busy retreiving member list.
			retrieved << "Retrieving member list (" << gdatap->mMembers.size()
					  << " / " << gdatap->mMemberCount << ")...";
		}
		else if( !gdatap->isRoleDataComplete() )
		{
			// Still busy retreiving role list.
			retrieved << "Retrieving role list (" << gdatap->mRoles.size()
					  << " / " << gdatap->mRoleCount << ")...";
		}
		else // (!gdatap->isRoleMemberDataComplete())
		{
			// Still busy retreiving role/member mappings.
			retrieved << "Retrieving role member mappings...";
		}
		mMembersList->setEnabled(FALSE);
		mMembersList->setCommentText(retrieved.str());
	}
}

void LLPanelGroupMembersSubTab::addMemberToList(LLGroupMemberData* data)
{
	if (!data) return;
	LLUIString donated = getString("donation_area");
	donated.setArg("[AREA]", llformat("%d", data->getContribution()));

	LLNameListCtrl::NameItem item_params;
	item_params.value = data->getID();

	item_params.columns.add().column("name").font.name("SANSSERIF_SMALL").style("NORMAL");

	item_params.columns.add().column("donated").value(donated.getString())
			.font.name("SANSSERIF_SMALL").style("NORMAL");

 	item_params.columns.add().column("online").value(data->getOnlineStatus())
 			.font.name("SANSSERIF_SMALL").style("NORMAL");
	
	mMembersList->addNameItemRow(item_params);

	mHasMatch = TRUE;
}

void LLPanelGroupMembersSubTab::onNameCache(const LLUUID& update_id, LLGroupMemberData* member, const LLAvatarName& av_name)
{
	mAvatarNameCacheConnection.disconnect();

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap
		|| gdatap->getMemberVersion() != update_id
		|| !member)
	{
		return;
	}
	
	// trying to avoid unnecessary hash lookups
	if (matchesSearchFilter(av_name.getAccountName()))
	{
		addMemberToList(member);
		if(!mMembersList->getEnabled())
		{
			mMembersList->setEnabled(TRUE);
		}
	}
	
}

void LLPanelGroupMembersSubTab::updateMembers()
{
	mPendingMemberUpdate = FALSE;

	// Rebuild the members list.

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap) 
	{
		llwarns << "LLPanelGroupMembersSubTab::updateMembers() -- No group data!" << llendl;
		return;
	}

	// Make sure all data is still complete.  Incomplete data
	// may occur if we refresh.
	if (   !gdatap->isMemberDataComplete() 
		|| !gdatap->isRoleDataComplete()
		|| !gdatap->isRoleMemberDataComplete())
	{
		return;
	}

	//cleanup list only for first iteration
	if(mMemberProgress == gdatap->mMembers.begin())
	{
		mMembersList->deleteAllItems();
	}

	LLGroupMgrGroupData::member_list_t::iterator end = gdatap->mMembers.end();

	LLTimer update_time;
	update_time.setTimerExpirySec(UPDATE_MEMBERS_SECONDS_PER_FRAME);

	for( ; mMemberProgress != end && !update_time.hasExpired(); ++mMemberProgress)
	{
		if (!mMemberProgress->second)
			continue;

		// Do filtering on name if it is already in the cache.
		LLAvatarName av_name;
		if (LLAvatarNameCache::get(mMemberProgress->first, &av_name))
		{
			if (matchesSearchFilter(av_name.getAccountName()))
			{
				addMemberToList(mMemberProgress->second);
			}
		}
		else
		{
			// If name is not cached, onNameCache() should be called when it is cached and add this member to list.
			// *TODO : Add one callback per fetched avatar name
			if (mAvatarNameCacheConnection.connected())
			{
				mAvatarNameCacheConnection.disconnect();
			}
			mAvatarNameCacheConnection = LLAvatarNameCache::get(mMemberProgress->first, boost::bind(&LLPanelGroupMembersSubTab::onNameCache, this, gdatap->getMemberVersion(), mMemberProgress->second, _2));
		}
	}

	if (mMemberProgress == end)
	{
		if (mHasMatch)
		{
			mMembersList->setEnabled(TRUE);
		}
		else
		{
			mMembersList->setEnabled(FALSE);
			mMembersList->setCommentText(std::string("No match."));
		}
	}
	else
	{
		mPendingMemberUpdate = TRUE;
	}

	// This should clear the other two lists, since nothing is selected.
	handleMemberSelect();
}

void LLPanelGroupMembersSubTab::onBanMember(void* user_data)
{
	LLPanelGroupMembersSubTab* self = static_cast<LLPanelGroupMembersSubTab*>(user_data);
	self->handleBanMember();
}

void LLPanelGroupMembersSubTab::handleBanMember()
{
	LLGroupMgrGroupData* gdatap	= LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if(!gdatap) 
	{
		LL_WARNS("Groups") << "Unable to get group data for group " << mGroupID << LL_ENDL;
		return;
	}

	std::vector<LLScrollListItem*> selection = mMembersList->getAllSelected();
	if(selection.empty())
	{
		return;
	}

	uuid_vec_t ban_ids;
	std::vector<LLScrollListItem*>::iterator itor;
	for(itor = selection.begin(); itor != selection.end(); ++itor)
	{
		LLUUID ban_id = (*itor)->getUUID();
		ban_ids.push_back(ban_id);
		
		LLGroupBanData ban_data;
		gdatap->createBanEntry(ban_id, ban_data);
	}	

	LLGroupMgr::getInstance()->sendGroupBanRequest(LLGroupMgr::REQUEST_POST, mGroupID, LLGroupMgr::BAN_CREATE, ban_ids);
	handleEjectMembers();
}


// LLPanelGroupRolesSubTab ///////////////////////////////////////////////
static LLRegisterPanelClassWrapper<LLPanelGroupRolesSubTab> t_panel_group_roles_subtab("panel_group_roles_subtab");

LLPanelGroupRolesSubTab::LLPanelGroupRolesSubTab()
  : LLPanelGroupSubTab(),
	mRolesList(NULL),
	mAssignedMembersList(NULL),
	mAllowedActionsList(NULL),
	mRoleName(NULL),
	mRoleTitle(NULL),
	mRoleDescription(NULL),
	mMemberVisibleCheck(NULL),
	mDeleteRoleButton(NULL),
	mCreateRoleButton(NULL),

	mHasRoleChange(FALSE)
{
}

LLPanelGroupRolesSubTab::~LLPanelGroupRolesSubTab()
{
}

BOOL LLPanelGroupRolesSubTab::postBuildSubTab(LLView* root)
{
	LLPanelGroupSubTab::postBuildSubTab(root);

	// Upcast parent so we can ask it for sibling controls.
	LLPanelGroupRoles* parent = (LLPanelGroupRoles*) root;

	// Look recursively from the parent to find all our widgets.
	bool recurse = true;
	mHeader = parent->getChild<LLPanel>("roles_header", recurse);
	mFooter = parent->getChild<LLPanel>("roles_footer", recurse);


	mRolesList 		= parent->getChild<LLScrollListCtrl>("role_list", recurse);
	mAssignedMembersList	= parent->getChild<LLNameListCtrl>("role_assigned_members", recurse);
	mAllowedActionsList	= parent->getChild<LLScrollListCtrl>("role_allowed_actions", recurse);

	mRoleName = parent->getChild<LLLineEditor>("role_name", recurse);
	mRoleTitle = parent->getChild<LLLineEditor>("role_title", recurse);
	mRoleDescription = parent->getChild<LLTextEditor>("role_description", recurse);

	mMemberVisibleCheck = parent->getChild<LLCheckBoxCtrl>("role_visible_in_list", recurse);

	if (!mRolesList || !mAssignedMembersList || !mAllowedActionsList
		|| !mRoleName || !mRoleTitle || !mRoleDescription || !mMemberVisibleCheck)
	{
		llwarns << "ARG! element not found." << llendl;
		return FALSE;
	}

	mRemoveEveryoneTxt = getString("cant_delete_role");

	mCreateRoleButton = 
		parent->getChild<LLButton>("role_create", recurse);
	if ( mCreateRoleButton )
	{
		mCreateRoleButton->setClickedCallback(onCreateRole, this);
		mCreateRoleButton->setEnabled(FALSE);
	}
	
	mDeleteRoleButton =  
		parent->getChild<LLButton>("role_delete", recurse);
	if ( mDeleteRoleButton )
	{
		mDeleteRoleButton->setClickedCallback(onDeleteRole, this);
		mDeleteRoleButton->setEnabled(FALSE);
	}

	mRolesList->setCommitOnSelectionChange(TRUE);
	mRolesList->setCommitCallback(onRoleSelect, this);

	mAssignedMembersList->setContextMenu(LLScrollListCtrl::MENU_AVATAR);

	mMemberVisibleCheck->setCommitCallback(onMemberVisibilityChange, this);

	mAllowedActionsList->setCommitOnSelectionChange(TRUE);

	mRoleName->setCommitOnFocusLost(TRUE);
	mRoleName->setKeystrokeCallback(onPropertiesKey, this);

	mRoleTitle->setCommitOnFocusLost(TRUE);
	mRoleTitle->setKeystrokeCallback(onPropertiesKey, this);

	mRoleDescription->setCommitOnFocusLost(TRUE);
	mRoleDescription->setKeystrokeCallback(boost::bind(&LLPanelGroupRolesSubTab::onDescriptionKeyStroke, this, _1));

	setFooterEnabled(FALSE);

	return TRUE;
}

void LLPanelGroupRolesSubTab::activate()
{
	LLPanelGroupSubTab::activate();

	mRolesList->deselectAllItems();
	mAssignedMembersList->deleteAllItems();
	mAllowedActionsList->deleteAllItems();
	mRoleName->clear();
	mRoleDescription->clear();
	mRoleTitle->clear();

	setFooterEnabled(FALSE);

	mHasRoleChange = FALSE;
	update(GC_ALL);
}

void LLPanelGroupRolesSubTab::deactivate()
{
	lldebugs << "LLPanelGroupRolesSubTab::deactivate()" << llendl;

	LLPanelGroupSubTab::deactivate();
}

bool LLPanelGroupRolesSubTab::needsApply(std::string& mesg)
{
	lldebugs << "LLPanelGroupRolesSubTab::needsApply()" << llendl;

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);

	return (mHasRoleChange								// Text changed in current role
			|| (gdatap && gdatap->pendingRoleChanges()));	// Pending role changes in the group
}

bool LLPanelGroupRolesSubTab::apply(std::string& mesg)
{
	lldebugs << "LLPanelGroupRolesSubTab::apply()" << llendl;

	saveRoleChanges(true);

	LLGroupMgr::getInstance()->sendGroupRoleChanges(mGroupID);

	notifyObservers();

	return true;
}

void LLPanelGroupRolesSubTab::cancel()
{
	mHasRoleChange = FALSE;
	LLGroupMgr::getInstance()->cancelGroupRoleChanges(mGroupID);

	notifyObservers();
}

LLSD LLPanelGroupRolesSubTab::createRoleItem(const LLUUID& role_id, 
								 std::string name, 
								 std::string title, 
								 S32 members)
{
	LLSD row;
	row["id"] = role_id;

	row["columns"][0]["column"] = "name";
	row["columns"][0]["value"] = name;

	row["columns"][1]["column"] = "title";
	row["columns"][1]["value"] = title;

	row["columns"][2]["column"] = "members";
	row["columns"][2]["value"] = members;
	
	return row;
}

bool LLPanelGroupRolesSubTab::matchesSearchFilter(std::string rolename, std::string roletitle)
{
	// If the search filter is empty, everything passes.
	if (mSearchFilter.empty()) return true;

	LLStringUtil::toLower(rolename);
	LLStringUtil::toLower(roletitle);
	std::string::size_type match_name = rolename.find(mSearchFilter);
	std::string::size_type match_title = roletitle.find(mSearchFilter);

	if ( (std::string::npos == match_name)
		&& (std::string::npos == match_title))
	{
		// not found
		return false;
	}
	else
	{
		return true;
	}
}

void LLPanelGroupRolesSubTab::update(LLGroupChange gc)
{
	lldebugs << "LLPanelGroupRolesSubTab::update()" << llendl;

	if (mGroupID.isNull()) return;

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);

	if (!gdatap || !gdatap->isRoleDataComplete())
	{
		LLGroupMgr::getInstance()->sendGroupRoleDataRequest(mGroupID);
	}
	else
	{
		bool had_selection = false;
		LLUUID last_selected;
		if (mRolesList->getFirstSelected())
		{
			last_selected = mRolesList->getFirstSelected()->getUUID();
			had_selection = true;
		}
		mRolesList->deleteAllItems();

		LLScrollListItem* item = NULL;

		LLGroupMgrGroupData::role_list_t::iterator rit = gdatap->mRoles.begin();
		LLGroupMgrGroupData::role_list_t::iterator end = gdatap->mRoles.end();

		for ( ; rit != end; ++rit)
		{
			LLRoleData rd;
			if (gdatap->getRoleData((*rit).first,rd))
			{
				if (matchesSearchFilter(rd.mRoleName, rd.mRoleTitle))
				{
					// If this is the everyone role, then EVERYONE is in it.
					S32 members_in_role = (*rit).first.isNull() ? gdatap->mMembers.size() : (*rit).second->getTotalMembersInRole();
					LLSD row = createRoleItem((*rit).first,rd.mRoleName, rd.mRoleTitle, members_in_role);
					item = mRolesList->addElement(row, ((*rit).first.isNull()) ? ADD_TOP : ADD_BOTTOM, this);
					if (had_selection && ((*rit).first == last_selected))
					{
						item->setSelected(TRUE);
					}
				}
			}
			else
			{
				llwarns << "LLPanelGroupRolesSubTab::update() No role data for role " << (*rit).first << llendl;
			}
		}

		mRolesList->sortByColumn(std::string("name"), TRUE);

		if ( (gdatap->mRoles.size() < (U32)MAX_ROLES)
			&& gAgent.hasPowerInGroup(mGroupID, GP_GROUP_BAN_ACCESS) )
		{
			mCreateRoleButton->setEnabled(TRUE);
		}
		else
		{
			mCreateRoleButton->setEnabled(FALSE);
		}

		if (had_selection)
		{
			handleRoleSelect();
		}
		else
		{
			mAssignedMembersList->deleteAllItems();
			mAllowedActionsList->deleteAllItems();
			mRoleName->clear();
			mRoleDescription->clear();
			mRoleTitle->clear();
			setFooterEnabled(FALSE);
			mDeleteRoleButton->setEnabled(FALSE);
		}
	}

	if (!gdatap || !gdatap->isMemberDataComplete())
	{
		LLGroupMgr::getInstance()->sendCapGroupMembersRequest(mGroupID);
	}
	
	if (!gdatap || !gdatap->isRoleMemberDataComplete())
	{
		LLGroupMgr::getInstance()->sendGroupRoleMembersRequest(mGroupID);
	}

	if ((GC_ROLE_MEMBER_DATA == gc || GC_MEMBER_DATA == gc)
	    && gdatap
	    && gdatap->isMemberDataComplete()
	    && gdatap->isRoleMemberDataComplete())
	{
		buildMembersList();
	}
}

// static
void LLPanelGroupRolesSubTab::onRoleSelect(LLUICtrl* ctrl, void* user_data)
{
	LLPanelGroupRolesSubTab* self = static_cast<LLPanelGroupRolesSubTab*>(user_data);
	if (!self) 
		return;

	self->handleRoleSelect();
}

void LLPanelGroupRolesSubTab::handleRoleSelect()
{
	BOOL can_delete = TRUE;
	lldebugs << "LLPanelGroupRolesSubTab::handleRoleSelect()" << llendl;

	mAssignedMembersList->deleteAllItems();
	mAllowedActionsList->deleteAllItems();
	
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap) 
	{
		llwarns << "LLPanelGroupRolesSubTab::handleRoleSelect() "
				<< "-- No group data!" << llendl;
		return;
	}

	saveRoleChanges(false);

	// Check if there is anything selected.
	LLScrollListItem* item = mRolesList->getFirstSelected();
	if (!item)
	{
		setFooterEnabled(FALSE);
		return;
	}

	setFooterEnabled(TRUE);

	LLRoleData rd;
	if (gdatap->getRoleData(item->getUUID(),rd))
	{
		BOOL is_owner_role = ( gdatap->mOwnerRole == item->getUUID() );
		mRoleName->setText(rd.mRoleName);
		mRoleTitle->setText(rd.mRoleTitle);
		mRoleDescription->setText(rd.mRoleDescription);

		mAllowedActionsList->setEnabled(gAgent.hasPowerInGroup(mGroupID,
									   GP_ROLE_CHANGE_ACTIONS));
		buildActionsList(mAllowedActionsList,
						 rd.mRolePowers,
						 0LL,
						 boost::bind(&LLPanelGroupRolesSubTab::handleActionCheck, this, _1, false),
						 TRUE,
						 FALSE,
						 is_owner_role);


		mMemberVisibleCheck->set((rd.mRolePowers & GP_MEMBER_VISIBLE_IN_DIR) == GP_MEMBER_VISIBLE_IN_DIR);
		mRoleName->setEnabled(!is_owner_role &&
				gAgent.hasPowerInGroup(mGroupID, GP_ROLE_PROPERTIES));
		mRoleTitle->setEnabled(gAgent.hasPowerInGroup(mGroupID, GP_ROLE_PROPERTIES));
		mRoleDescription->setEnabled(gAgent.hasPowerInGroup(mGroupID, GP_ROLE_PROPERTIES));
		
		if ( is_owner_role ) 
			{
				// you can't delete the owner role
				can_delete = FALSE;
				// ... or hide members with this role
				mMemberVisibleCheck->setEnabled(FALSE);
			}
		else
			{
				mMemberVisibleCheck->setEnabled(gAgent.hasPowerInGroup(mGroupID, GP_ROLE_PROPERTIES));
			}
		
		if (item->getUUID().isNull())
		{
			// Everyone role, can't edit description or name or delete
			mRoleDescription->setEnabled(FALSE);
			mRoleName->setEnabled(FALSE);
			can_delete = FALSE;
		}
	}
	else
	{
		mRolesList->deselectAllItems();
		mAssignedMembersList->deleteAllItems();
		mAllowedActionsList->deleteAllItems();
		mRoleName->clear();
		mRoleDescription->clear();
		mRoleTitle->clear();
		setFooterEnabled(FALSE);

		can_delete = FALSE;
	}
	mSelectedRole = item->getUUID();
	buildMembersList();

	can_delete = can_delete && gAgent.hasPowerInGroup(mGroupID,
													  GP_ROLE_DELETE);
	mDeleteRoleButton->setEnabled(can_delete);
}

void LLPanelGroupRolesSubTab::buildMembersList()
{
	mAssignedMembersList->deleteAllItems();

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap) 
	{
		llwarns << "LLPanelGroupRolesSubTab::handleRoleSelect() "
				<< "-- No group data!" << llendl;
		return;
	}

	// Check if there is anything selected.
	LLScrollListItem* item = mRolesList->getFirstSelected();
	if (!item) return;

	if (item->getUUID().isNull())
	{
		// Special cased 'Everyone' role
		LLGroupMgrGroupData::member_list_t::iterator mit = gdatap->mMembers.begin();
		LLGroupMgrGroupData::member_list_t::iterator end = gdatap->mMembers.end();
		for ( ; mit != end; ++mit)
		{
			mAssignedMembersList->addNameItem((*mit).first);
		}
	}
	else
	{
		LLGroupMgrGroupData::role_list_t::iterator rit = gdatap->mRoles.find(item->getUUID());
		if (rit != gdatap->mRoles.end())
		{
			LLGroupRoleData* rdatap = (*rit).second;
			if (rdatap)
			{
				uuid_vec_t::const_iterator mit = rdatap->getMembersBegin();
				uuid_vec_t::const_iterator end = rdatap->getMembersEnd();
				for ( ; mit != end; ++mit)
				{
					mAssignedMembersList->addNameItem((*mit));
				}
			}
		}
	}
}

struct ActionCBData
{
	LLPanelGroupRolesSubTab*	mSelf;
	LLCheckBoxCtrl* 			mCheck;
};

void LLPanelGroupRolesSubTab::handleActionCheck(LLUICtrl* ctrl, bool force)
{
	LLCheckBoxCtrl* check = dynamic_cast<LLCheckBoxCtrl*>(ctrl);
	if (!check)
		return;
	
	lldebugs << "LLPanelGroupRolesSubTab::handleActionSelect()" << llendl;

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap) 
	{
		llwarns << "LLPanelGroupRolesSubTab::handleRoleSelect() "
				<< "-- No group data!" << llendl;
		return;
	}

	LLScrollListItem* action_item = mAllowedActionsList->getFirstSelected();
	if (!action_item)
	{
		return;
	}

	LLScrollListItem* role_item = mRolesList->getFirstSelected();
	if (!role_item)
	{
		return;
	}
	LLUUID role_id = role_item->getUUID();

	LLRoleAction* rap = (LLRoleAction*)action_item->getUserdata();
	U64 power = rap->mPowerBit;

	if (check->get())
	{
		if (!force && (    (GP_ROLE_ASSIGN_MEMBER == power)
						|| (GP_ROLE_CHANGE_ACTIONS == power) ))
		{
			// Uncheck the item, for now.  It will be
			// checked if they click 'Yes', below.
			check->set(FALSE);

			LLRoleData rd;
			LLSD args;

			if ( gdatap->getRoleData(role_id, rd) )
			{
				args["ACTION_NAME"] = rap->mDescription;
				args["ROLE_NAME"] = rd.mRoleName;
				mHasModal = TRUE;
				std::string warning = "AssignDangerousActionWarning";
				if (GP_ROLE_CHANGE_ACTIONS == power)
				{
					warning = "AssignDangerousAbilityWarning";
				}
				LLNotificationsUtil::add(warning, args, LLSD(), boost::bind(&LLPanelGroupRolesSubTab::addActionCB, this, _1, _2, check));
			}
			else
			{
				llwarns << "Unable to look up role information for role id: "
						<< role_id << llendl;
			}
		}
		else
		{
			gdatap->addRolePower(role_id,power);
		}
	}
	else
	{
		gdatap->removeRolePower(role_id,power);
	}

	mHasRoleChange = TRUE;
	notifyObservers();
}

bool LLPanelGroupRolesSubTab::addActionCB(const LLSD& notification, const LLSD& response, LLCheckBoxCtrl* check)
{
	if (!check) return false;

	mHasModal = FALSE;

	S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
	if (0 == option)
	{
		// User clicked "Yes"
		check->set(TRUE);
		const bool force_add = true;
		handleActionCheck(check, force_add);
	}
	return false;
}

// static
void LLPanelGroupRolesSubTab::onPropertiesKey(LLLineEditor* ctrl, void* user_data)
{
	LLPanelGroupRolesSubTab* self = static_cast<LLPanelGroupRolesSubTab*>(user_data);
	if (!self) return;

	self->mHasRoleChange = TRUE;
	self->notifyObservers();
}

void LLPanelGroupRolesSubTab::onDescriptionKeyStroke(LLTextEditor* caller)
{
	mHasRoleChange = TRUE;
	notifyObservers();
}

// static 
void LLPanelGroupRolesSubTab::onDescriptionCommit(LLUICtrl* ctrl, void* user_data)
{
	LLPanelGroupRolesSubTab* self = static_cast<LLPanelGroupRolesSubTab*>(user_data);
	if (!self) return;

	self->mHasRoleChange = TRUE;
	self->notifyObservers();
}

// static 
void LLPanelGroupRolesSubTab::onMemberVisibilityChange(LLUICtrl* ctrl, void* user_data)
{
	LLPanelGroupRolesSubTab* self = static_cast<LLPanelGroupRolesSubTab*>(user_data);
	LLCheckBoxCtrl* check = static_cast<LLCheckBoxCtrl*>(ctrl);
	if (!check || !self) return;

	self->handleMemberVisibilityChange(check->get());
}

void LLPanelGroupRolesSubTab::handleMemberVisibilityChange(bool value)
{
	lldebugs << "LLPanelGroupRolesSubTab::handleMemberVisibilityChange()" << llendl;

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if (!gdatap) 
	{
		llwarns << "LLPanelGroupRolesSubTab::handleRoleSelect() "
				<< "-- No group data!" << llendl;
		return;
	}

	LLScrollListItem* role_item = mRolesList->getFirstSelected();
	if (!role_item)
	{
		return;
	}

	if (value)
	{
		gdatap->addRolePower(role_item->getUUID(),GP_MEMBER_VISIBLE_IN_DIR);
	}
	else
	{
		gdatap->removeRolePower(role_item->getUUID(),GP_MEMBER_VISIBLE_IN_DIR);
	}
}

// static 
void LLPanelGroupRolesSubTab::onCreateRole(void* user_data)
{
	LLPanelGroupRolesSubTab* self = static_cast<LLPanelGroupRolesSubTab*>(user_data);
	if (!self) return;

	self->handleCreateRole();
}

void LLPanelGroupRolesSubTab::handleCreateRole()
{
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);

	if (!gdatap) return;

	LLUUID new_role_id;
	new_role_id.generate();

	LLRoleData rd;
	rd.mRoleName = "New Role";
	gdatap->createRole(new_role_id,rd);

	mRolesList->deselectAllItems(TRUE);
	LLSD row;
	row["id"] = new_role_id;
	row["columns"][0]["column"] = "name";
	row["columns"][0]["value"] = rd.mRoleName;
	mRolesList->addElement(row, ADD_BOTTOM, this);
	mRolesList->selectByID(new_role_id);

	// put focus on name field and select its contents
	if(mRoleName)
	{
		mRoleName->setFocus(TRUE);
		mRoleName->onTabInto();
		gFocusMgr.triggerFocusFlash();
	}

	notifyObservers();
}

// static 
void LLPanelGroupRolesSubTab::onDeleteRole(void* user_data)
{
	LLPanelGroupRolesSubTab* self = static_cast<LLPanelGroupRolesSubTab*>(user_data);
	if (!self) return;

	self->handleDeleteRole();
}

void LLPanelGroupRolesSubTab::handleDeleteRole()
{
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);

	if (!gdatap) return;

	LLScrollListItem* role_item = mRolesList->getFirstSelected();
	if (!role_item)
	{
		return;
	}

	if (role_item->getUUID().isNull() || role_item->getUUID() == gdatap->mOwnerRole)
	{
		LLSD args;
		args["MESSAGE"] = mRemoveEveryoneTxt;
		LLNotificationsUtil::add("GenericAlert", args);
		return;
	}

	gdatap->deleteRole(role_item->getUUID());
	mRolesList->deleteSingleItem(mRolesList->getFirstSelectedIndex());
	mRolesList->selectFirstItem();

	notifyObservers();
}

void LLPanelGroupRolesSubTab::saveRoleChanges(bool select_saved_role)
{
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);

	if (!gdatap) return;

	if (mHasRoleChange)
	{
		LLRoleData rd;
		if (!gdatap->getRoleData(mSelectedRole,rd)) return;

		rd.mRoleName = mRoleName->getText();
		rd.mRoleDescription = mRoleDescription->getText();
		rd.mRoleTitle = mRoleTitle->getText();

		S32 role_members_count = 0;
		if (mSelectedRole.isNull())
		{
			role_members_count = gdatap->mMemberCount;
		}
		else if(LLGroupRoleData* grd = get_ptr_in_map(gdatap->mRoles, mSelectedRole))
		{
			role_members_count = grd->getTotalMembersInRole();
		}

		gdatap->setRoleData(mSelectedRole,rd);

		mRolesList->deleteSingleItem(mRolesList->getItemIndex(mSelectedRole));
		
		LLSD row = createRoleItem(mSelectedRole,rd.mRoleName,rd.mRoleTitle,role_members_count);
		LLScrollListItem* item = mRolesList->addElement(row, ADD_BOTTOM, this);
		item->setSelected(select_saved_role);

		mHasRoleChange = FALSE;
	}
}

void LLPanelGroupRolesSubTab::setGroupID(const LLUUID& id)
{
	if(mRolesList) mRolesList->deleteAllItems();
	if(mAssignedMembersList) mAssignedMembersList->deleteAllItems();
	if(mAllowedActionsList) mAllowedActionsList->deleteAllItems();

	if(mRoleName) mRoleName->clear();
	if(mRoleDescription) mRoleDescription->clear();
	if(mRoleTitle) mRoleTitle->clear();

	mHasRoleChange = FALSE;

	setFooterEnabled(FALSE);

	LLPanelGroupSubTab::setGroupID(id);
}


// LLPanelGroupActionsSubTab /////////////////////////////////////////////
static LLRegisterPanelClassWrapper<LLPanelGroupActionsSubTab> t_panel_group_actions_subtab("panel_group_actions_subtab");

LLPanelGroupActionsSubTab::LLPanelGroupActionsSubTab()
: LLPanelGroupSubTab()
{
}

LLPanelGroupActionsSubTab::~LLPanelGroupActionsSubTab()
{
}

BOOL LLPanelGroupActionsSubTab::postBuildSubTab(LLView* root)
{
	LLPanelGroupSubTab::postBuildSubTab(root);

	// Upcast parent so we can ask it for sibling controls.
	LLPanelGroupRoles* parent = (LLPanelGroupRoles*) root;

	// Look recursively from the parent to find all our widgets.
	bool recurse = true;
	mHeader = parent->getChild<LLPanel>("actions_header", recurse);
	mFooter = parent->getChild<LLPanel>("actions_footer", recurse);

	mActionDescription = parent->getChild<LLTextEditor>("action_description", recurse);

	mActionList = parent->getChild<LLScrollListCtrl>("action_list",recurse);
	mActionRoles = parent->getChild<LLScrollListCtrl>("action_roles",recurse);
	mActionMembers  = parent->getChild<LLNameListCtrl>("action_members",recurse);

	if (!mActionList || !mActionDescription || !mActionRoles || !mActionMembers) return FALSE;

	mActionList->setCommitOnSelectionChange(TRUE);
	mActionList->setCommitCallback(boost::bind(&LLPanelGroupActionsSubTab::handleActionSelect, this));
	mActionList->setContextMenu(LLScrollListCtrl::MENU_AVATAR);

	update(GC_ALL);

	return TRUE;
}

void LLPanelGroupActionsSubTab::activate()
{
	LLPanelGroupSubTab::activate();

	update(GC_ALL);
}

void LLPanelGroupActionsSubTab::deactivate()
{
	lldebugs << "LLPanelGroupActionsSubTab::deactivate()" << llendl;

	LLPanelGroupSubTab::deactivate();
}

bool LLPanelGroupActionsSubTab::needsApply(std::string& mesg)
{
	lldebugs << "LLPanelGroupActionsSubTab::needsApply()" << llendl;

	return false;
}

bool LLPanelGroupActionsSubTab::apply(std::string& mesg)
{
	lldebugs << "LLPanelGroupActionsSubTab::apply()" << llendl;
	return true;
}

void LLPanelGroupActionsSubTab::update(LLGroupChange gc)
{
	lldebugs << "LLPanelGroupActionsSubTab::update()" << llendl;

	if (mGroupID.isNull()) return;

	mActionList->deselectAllItems();
	mActionMembers->deleteAllItems();
	mActionRoles->deleteAllItems();
	mActionDescription->clear();

	mActionList->deleteAllItems();
	buildActionsList(mActionList,
					 GP_ALL_POWERS,
					 GP_ALL_POWERS,
					 NULL,
					 FALSE,
					 TRUE,
					 FALSE);
}

void LLPanelGroupActionsSubTab::handleActionSelect()
{
	mActionMembers->deleteAllItems();
	mActionRoles->deleteAllItems();

	U64 power_mask = GP_NO_POWERS;
	std::vector<LLScrollListItem*> selection = 
							mActionList->getAllSelected();
	if (selection.empty()) return;

	LLRoleAction* rap;

	std::vector<LLScrollListItem*>::iterator itor;
	for (itor = selection.begin() ; 
		 itor != selection.end(); ++itor)
	{
		rap = (LLRoleAction*)( (*itor)->getUserdata() );
		power_mask |= rap->mPowerBit;
	}

	if (selection.size() == 1)
	{
		LLScrollListItem* item = selection[0];
		rap = (LLRoleAction*)(item->getUserdata());

		if (rap->mLongDescription.empty())
		{
			mActionDescription->setText(rap->mDescription);
		}
		else
		{
			mActionDescription->setText(rap->mLongDescription);
		}
	}
	else
	{
		mActionDescription->clear();
	}

	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);

	if (!gdatap) return;

	if (gdatap->isMemberDataComplete())
	{
		LLGroupMgrGroupData::member_list_t::iterator it = gdatap->mMembers.begin();
		LLGroupMgrGroupData::member_list_t::iterator end = gdatap->mMembers.end();
		LLGroupMemberData* gmd;

		for ( ; it != end; ++it)
		{
			gmd = (*it).second;
			if (!gmd) continue;
			if ((gmd->getAgentPowers() & power_mask) == power_mask)
			{
				mActionMembers->addNameItem(gmd->getID());
			}
		}
	}
	else
	{
		LLGroupMgr::getInstance()->sendCapGroupMembersRequest(mGroupID);
	}

	if (gdatap->isRoleDataComplete())
	{
		LLGroupMgrGroupData::role_list_t::iterator it = gdatap->mRoles.begin();
		LLGroupMgrGroupData::role_list_t::iterator end = gdatap->mRoles.end();
		LLGroupRoleData* rmd;

		for ( ; it != end; ++it)
		{
			rmd = (*it).second;
			if (!rmd) continue;
			if ((rmd->getRoleData().mRolePowers & power_mask) == power_mask)
			{
				mActionRoles->addSimpleElement(rmd->getRoleData().mRoleName);
			}
		}
	}
	else
	{
		LLGroupMgr::getInstance()->sendGroupRoleDataRequest(mGroupID);
	}
}

void LLPanelGroupActionsSubTab::setGroupID(const LLUUID& id)
{
	if(mActionList) mActionList->deleteAllItems();
	if(mActionRoles) mActionRoles->deleteAllItems();
	if(mActionMembers) mActionMembers->deleteAllItems();

	if(mActionDescription) mActionDescription->clear();

	LLPanelGroupSubTab::setGroupID(id);
}


// LLPanelGroupBanListSubTab /////////////////////////////////////////////
static LLRegisterPanelClassWrapper<LLPanelGroupBanListSubTab> t_panel_group_ban_subtab("panel_group_banlist_subtab");

LLPanelGroupBanListSubTab::LLPanelGroupBanListSubTab()
	: LLPanelGroupSubTab(),
	  mBanList(NULL),
	  mCreateBanButton(NULL),
	  mDeleteBanButton(NULL)
{}

BOOL LLPanelGroupBanListSubTab::postBuildSubTab(LLView* root)
{
	LLPanelGroupSubTab::postBuildSubTab(root);

	// Upcast parent so we can ask it for sibling controls.
	LLPanelGroupRoles* parent = (LLPanelGroupRoles*)root;

	// Look recursively from the parent to find all our widgets.
	bool recurse = true;
	
	mHeader	= parent->getChild<LLPanel>("banlist_header", recurse);
	mFooter	= parent->getChild<LLPanel>("banlist_footer", recurse);
	
	mBanList = parent->getChild<LLNameListCtrl>("ban_list", recurse);
	
	mCreateBanButton		= parent->getChild<LLButton>("ban_create", recurse);
	mDeleteBanButton		= parent->getChild<LLButton>("ban_delete", recurse);
	mRefreshBanListButton	= parent->getChild<LLButton>("ban_refresh", recurse);

	if(!mBanList || !mCreateBanButton || !mDeleteBanButton || !mRefreshBanListButton)
		return FALSE;

	mBanList->setCommitOnSelectionChange(TRUE);
	mBanList->setCommitCallback(onBanEntrySelect, this);

	mCreateBanButton->setClickedCallback(onCreateBanEntry, this);
	mCreateBanButton->setEnabled(TRUE);

	mDeleteBanButton->setClickedCallback(onDeleteBanEntry, this);
	mDeleteBanButton->setEnabled(FALSE);
	
	mRefreshBanListButton->setClickedCallback(onRefreshBanList, this);
	mRefreshBanListButton->setEnabled(FALSE);

	mBanList->setOnNameListCompleteCallback(boost::bind(&LLPanelGroupBanListSubTab::onBanListCompleted, this, _1));
	
	setFooterEnabled(FALSE);
	return TRUE;
}

void LLPanelGroupBanListSubTab::activate()
{
	LLPanelGroupSubTab::activate();

	mBanList->deselectAllItems();
	mDeleteBanButton->setEnabled(FALSE);

	// BAKER: Should I really request everytime activate() is called?
	//		  Perhaps I should only do it on a force refresh, or if an action on the list happens...
	//		  Because it's not going to live-update the list anyway... You'd have to refresh if you 
	//		  wanted to see someone else's additions anyway...
	//		  
	LLGroupMgr::getInstance()->sendGroupBanRequest(LLGroupMgr::REQUEST_GET, mGroupID);

	setFooterEnabled(FALSE);
	update(GC_ALL);
}

void LLPanelGroupBanListSubTab::update(LLGroupChange gc)
{
	populateBanList();
}

void LLPanelGroupBanListSubTab::draw()
{
	LLPanelGroupSubTab::draw();

	// BAKER: Might be good to put it here instead of update, maybe.. See how often draw gets hit.
	//	populateBanList();
}

void LLPanelGroupBanListSubTab::onBanEntrySelect(LLUICtrl* ctrl, void* user_data)
{
	LLPanelGroupBanListSubTab* self = static_cast<LLPanelGroupBanListSubTab*>(user_data);
	if (!self) 
		return;

	self->handleBanEntrySelect();
}

void LLPanelGroupBanListSubTab::handleBanEntrySelect()
{
	if (gAgent.hasPowerInGroup(mGroupID, GP_GROUP_BAN_ACCESS))
		mDeleteBanButton->setEnabled(TRUE);
}

void LLPanelGroupBanListSubTab::onCreateBanEntry(void* user_data)
{
	LLPanelGroupBanListSubTab* self = static_cast<LLPanelGroupBanListSubTab*>(user_data);
	if (!self) 
		return;

	self->handleCreateBanEntry();
}

void LLPanelGroupBanListSubTab::handleCreateBanEntry()
{
	LLFloaterGroupBulkBan::showForGroup(mGroupID);
	populateBanList();
}

void LLPanelGroupBanListSubTab::onDeleteBanEntry(void* user_data)
{
	LLPanelGroupBanListSubTab* self = static_cast<LLPanelGroupBanListSubTab*>(user_data);
	if (!self) 
		return;

	self->handleDeleteBanEntry();
}

void LLPanelGroupBanListSubTab::handleDeleteBanEntry()
{
	LLGroupMgrGroupData* gdatap	= LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if(!gdatap) 
	{
		LL_WARNS("Groups") << "Unable to get group data for group " << mGroupID << LL_ENDL;
		return;
	}

	std::vector<LLScrollListItem*> selection = mBanList->getAllSelected();
	if(selection.empty())
	{
		return;
	}

	bool can_ban_members = false;
	if (gAgent.isGodlike() ||
		gAgent.hasPowerInGroup(mGroupID, GP_GROUP_BAN_ACCESS))
	{
		can_ban_members	= true;
	}
	
	// Owners can ban anyone in the group.
	LLGroupMgrGroupData::member_list_t::iterator mi = gdatap->mMembers.find(gAgent.getID());
	if (mi != gdatap->mMembers.end())
	{
		LLGroupMemberData* member_data = (*mi).second;
		if ( member_data && member_data->isInRole(gdatap->mOwnerRole) )
		{
			can_ban_members	= true;
		}
	}
		
	if(!can_ban_members)
		return;

	std::vector<LLUUID> ban_ids;
	std::vector<LLScrollListItem*>::iterator itor;
	for(itor = selection.begin(); itor != selection.end(); ++itor)
	{
		LLUUID ban_id = (*itor)->getUUID();
		ban_ids.push_back(ban_id);
		
		gdatap->removeBanEntry(ban_id);
		mBanList->removeNameItem(ban_id);
	
		// Removing an item removes the selection, we shouldn't be able to click
		// the button anymore until we reselect another entry.
		mDeleteBanButton->setEnabled(FALSE);
	}
	
	LLGroupMgr::getInstance()->sendGroupBanRequest(LLGroupMgr::REQUEST_POST, mGroupID, LLGroupMgr::BAN_DELETE, ban_ids);
}

void LLPanelGroupBanListSubTab::onRefreshBanList(void* user_data)
{
	LLPanelGroupBanListSubTab* self = static_cast<LLPanelGroupBanListSubTab*>(user_data);
	if (!self) 
		return;

	self->handleRefreshBanList();
}

void LLPanelGroupBanListSubTab::handleRefreshBanList()
{
	mRefreshBanListButton->setEnabled(FALSE);
	LLGroupMgr::getInstance()->sendGroupBanRequest(LLGroupMgr::REQUEST_GET, mGroupID);
}

void LLPanelGroupBanListSubTab::onBanListCompleted(bool isComplete)
{
	if(isComplete)
	{
		mRefreshBanListButton->setEnabled(TRUE);
		populateBanList();
	}
}

void LLPanelGroupBanListSubTab::populateBanList()
{
	LLGroupMgrGroupData* gdatap = LLGroupMgr::getInstance()->getGroupData(mGroupID);
	if(!gdatap) 
	{
		LL_WARNS("Groups") << "Unable to get group data for group " << mGroupID << LL_ENDL;
		return;
	}

	mBanList->deleteAllItems();
	std::map<LLUUID,LLGroupBanData>::const_iterator entry = gdatap->mBanList.begin();
	for(; entry != gdatap->mBanList.end(); entry++)
	{
		LLNameListCtrl::NameItem ban_entry;
		ban_entry.value = entry->first;
		LLGroupBanData bd = entry->second;
		
		ban_entry.columns.add().column("name").font.name("SANSSERIF_SMALL").style("NORMAL");
		ban_entry.columns.add().column("ban_date").value(bd.mBanDate.toHTTPDateString("%Y/%m/%d")).font.name("SANSSERIF_SMALL").style("NORMAL");

		mBanList->addNameItemRow(ban_entry);
	}
	 
	mRefreshBanListButton->setEnabled(TRUE);
}

void LLPanelGroupBanListSubTab::setGroupID(const LLUUID& id)
{
	if(mBanList)
		mBanList->deleteAllItems();

	setFooterEnabled(FALSE);
	LLPanelGroupSubTab::setGroupID(id); 
}

