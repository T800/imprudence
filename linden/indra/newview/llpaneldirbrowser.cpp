/** 
 * @file llpaneldirbrowser.cpp
 * @brief LLPanelDirBrowser class implementation
 *
 * Copyright (c) 2001-2007, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlife.com/developers/opensource/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at http://secondlife.com/developers/opensource/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 */

// Base class for the various search panels/results browsers
// in the Find floater.  For example, Find > Popular Places
// is derived from this.

#include "llviewerprecompiledheaders.h"

#include "llpaneldirbrowser.h"

// linden library includes
#include "lldir.h"
#include "lleventflags.h"
#include "llfontgl.h"
#include "llqueryflags.h"
#include "message.h"

// viewer project includes
#include "llagent.h"
#include "llbutton.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llfloateravatarinfo.h"
#include "llfloaterdirectory.h" 
#include "lllineeditor.h"
#include "llmenucommands.h"
#include "llmenugl.h"
#include "llpanelavatar.h"
#include "llpanelevent.h"
#include "llpanelgroup.h"
#include "llpanelclassified.h"
#include "llpanelpick.h"
#include "llpanelplace.h"
#include "llpaneldirland.h"
#include "llscrolllistctrl.h"
#include "lltextbox.h"
#include "lluiconstants.h"
#include "llviewercontrol.h"
#include "llviewerimagelist.h"
#include "llviewermessage.h"
#include "llvieweruictrlfactory.h"


//
// Globals
//

LLMap< const LLUUID, LLPanelDirBrowser* > gDirBrowserInstances;

LLPanelDirBrowser::LLPanelDirBrowser(const std::string& name, LLFloaterDirectory* floater)
:	LLPanel(name),
	mSearchID(),
	mWantSelectID(),
	mCurrentSortColumn("name"),
	mCurrentSortAscending(TRUE),
	mSearchStart(0),
	mResultsPerPage(100),
	mResultsReceived(0),
	mMinSearchChars(1),
	mResultsContents(),
	mHaveSearchResults(FALSE),
	mDidAutoSelect(TRUE),
	mLastResultTimer(),
	mFloaterDirectory(floater)
{
	//updateResultCount();
}

BOOL LLPanelDirBrowser::postBuild()
{
	childSetCommitCallback("results", onCommitList, this);

	childSetAction("< Prev", onClickPrev, this);
	childHide("< Prev");

	childSetAction("Next >", onClickNext, this);
	childHide("Next >");

	return TRUE;
}

LLPanelDirBrowser::~LLPanelDirBrowser()
{
	// Children all cleaned up by default view destructor.

	gDirBrowserInstances.removeData(mSearchID);
}


// virtual
void LLPanelDirBrowser::draw()
{
	// HACK: If the results panel has data, we want to select the first
	// item.  Unfortunately, we don't know when the find is actually done,
	// so only do this if it's been some time since the last packet of
	// results was received.
	if (getVisible() && mLastResultTimer.getElapsedTimeF32() > 0.5)
	{
		if (!mDidAutoSelect &&
			!childHasFocus("results"))
		{
			LLCtrlListInterface *list = childGetListInterface("results");
			if (list)
			{
				if (list->getCanSelect())
				{
					list->selectFirstItem(); // select first item by default
					childSetFocus("results", TRUE);
				}
				// Request specific data from the server
				onCommitList(NULL, this);
			}
		}
		mDidAutoSelect = TRUE;
	}
	
	LLPanel::draw();
}


// virtual
void LLPanelDirBrowser::nextPage()
{
	mSearchStart += mResultsPerPage;
	childShow("< Prev");

	performQuery();
}


// virtual
void LLPanelDirBrowser::prevPage()
{
	mSearchStart -= mResultsPerPage;
	childSetVisible("< Prev", mSearchStart > 0);

	performQuery();
}


void LLPanelDirBrowser::resetSearchStart()
{
	mSearchStart = 0;
	childHide("Next >");
	childHide("< Prev");
}

// protected
void LLPanelDirBrowser::updateResultCount()
{
	LLCtrlListInterface *list = childGetListInterface("results");
	if (!list) return;

	S32 result_count = list->getItemCount();
	LLString result_text;

	if (!mHaveSearchResults) result_count = 0;

	if (childIsVisible("Next >")) 
	{
		// Item count be off by a few if bogus items sent from database
		// Just use the number of results per page. JC
		result_text = llformat(">%d found", mResultsPerPage);
	}
	else 
	{
		result_text = llformat("%d found", result_count);
	}
	
	childSetValue("result_text", result_text);
	
	if (result_count == 0)
	{
		// add none found response
		if (list->getItemCount() == 0)
		{
			list->addSimpleElement("None found.");
			list->operateOnAll(LLCtrlListInterface::OP_DESELECT);
		}
	}
	else
	{
		childEnable("results");
	}
}

// static
void LLPanelDirBrowser::onClickPrev(void* data)
{
	LLPanelDirBrowser* self = (LLPanelDirBrowser*)data;
	self->prevPage();
}


// static
void LLPanelDirBrowser::onClickNext(void* data)
{
	LLPanelDirBrowser* self = (LLPanelDirBrowser*)data;
	self->nextPage();
}


void LLPanelDirBrowser::selectByUUID(const LLUUID& id)
{
	LLCtrlListInterface *list = childGetListInterface("results");
	if (!list) return;
	BOOL found = list->setCurrentByID(id);
	if (found)
	{
		// we got it, don't wait for network
		// Don't bother looking for this in the draw loop.
		mWantSelectID.setNull();
		// Make sure UI updates.
		onCommitList(NULL, this);
	}
	else
	{
		// waiting for this item from the network
		mWantSelectID = id;
	}
}

void LLPanelDirBrowser::selectEventByID(S32 event_id)
{
	if (mFloaterDirectory)
	{
		if (mFloaterDirectory->mPanelEventp)
		{
			mFloaterDirectory->mPanelEventp->setVisible(TRUE);
			mFloaterDirectory->mPanelEventp->setEventID(event_id);
		}
	}
}

U32 LLPanelDirBrowser::getSelectedEventID() const
{
	if (mFloaterDirectory)
	{
		if (mFloaterDirectory->mPanelEventp)
		{
			return mFloaterDirectory->mPanelEventp->getEventID();
		}
	}
	return 0;
}

void LLPanelDirBrowser::getSelectedInfo(LLUUID* id, S32 *type)
{
	LLCtrlListInterface *list = childGetListInterface("results");
	if (!list) return;

	LLSD id_sd = childGetValue("results");

	*id = id_sd.asUUID();

	LLString id_str = id_sd.asString();
	*type = mResultsContents[id_str]["type"];
}


// static
void LLPanelDirBrowser::onCommitList(LLUICtrl* ctrl, void* data)
{
	LLPanelDirBrowser* self = (LLPanelDirBrowser*)data;
	LLCtrlListInterface *list = self->childGetListInterface("results");
	if (!list) return;

	// Start with everyone invisible
	if (self->mFloaterDirectory)
	{
		if (self->mFloaterDirectory->mPanelAvatarp)	 self->mFloaterDirectory->mPanelAvatarp->setVisible(FALSE);
		if (self->mFloaterDirectory->mPanelEventp) self->mFloaterDirectory->mPanelEventp->setVisible(FALSE);
		if (self->mFloaterDirectory->mPanelGroupp) self->mFloaterDirectory->mPanelGroupp->setVisible(FALSE);
		if (self->mFloaterDirectory->mPanelGroupHolderp) self->mFloaterDirectory->mPanelGroupHolderp->setVisible(FALSE);
		if (self->mFloaterDirectory->mPanelPlacep) self->mFloaterDirectory->mPanelPlacep->setVisible(FALSE);
		if (self->mFloaterDirectory->mPanelPlaceSmallp) self->mFloaterDirectory->mPanelPlaceSmallp->setVisible(FALSE);
		if (self->mFloaterDirectory->mPanelClassifiedp) self->mFloaterDirectory->mPanelClassifiedp->setVisible(FALSE);
	}
	
	if (FALSE == list->getCanSelect())
	{
		return;
	}

	LLString id_str = self->childGetValue("results").asString();
	if (id_str.empty())
	{
		return;
	}

	LLUUID id = list->getCurrentID();
	S32 type = self->mResultsContents[id_str]["type"];
	LLString name = self->mResultsContents[id_str]["name"].asString();
	
	switch(type)
	{
	// These are both people searches.  Let the panel decide if they are online or offline.
	case ONLINE_CODE:
	case OFFLINE_CODE:
		if (self->mFloaterDirectory && self->mFloaterDirectory->mPanelAvatarp)
		{
			self->mFloaterDirectory->mPanelAvatarp->setVisible(TRUE);
			self->mFloaterDirectory->mPanelAvatarp->setAvatarID(id, name, ONLINE_STATUS_NO);
		}
		break;
	case EVENT_CODE:
		{
			U32 event_id = (U32)self->mResultsContents[id_str]["event_id"].asInteger();
			self->showEvent(event_id);
		}
		break;
	case GROUP_CODE:
		if (self->mFloaterDirectory && self->mFloaterDirectory->mPanelGroupHolderp)
		{
			self->mFloaterDirectory->mPanelGroupHolderp->setVisible(TRUE);
		}
		if (self->mFloaterDirectory && self->mFloaterDirectory->mPanelGroupp)
		{
			self->mFloaterDirectory->mPanelGroupp->setVisible(TRUE);
			self->mFloaterDirectory->mPanelGroupp->setGroupID(id);
		}
		break;
	case CLASSIFIED_CODE:
		if (self->mFloaterDirectory && self->mFloaterDirectory->mPanelClassifiedp)
		{
			self->mFloaterDirectory->mPanelClassifiedp->setVisible(TRUE);
			self->mFloaterDirectory->mPanelClassifiedp->setClassifiedID(id);
			self->mFloaterDirectory->mPanelClassifiedp->sendClassifiedInfoRequest();
		}
		break;
	case FOR_SALE_CODE:
	case AUCTION_CODE:
		if (self->mFloaterDirectory && self->mFloaterDirectory->mPanelPlaceSmallp)
		{
			self->mFloaterDirectory->mPanelPlaceSmallp->setVisible(TRUE);
			self->mFloaterDirectory->mPanelPlaceSmallp->setParcelID(id);
			self->mFloaterDirectory->mPanelPlaceSmallp->sendParcelInfoRequest();
		}
		break;
	case PLACE_CODE:
	case POPULAR_CODE:
		if (self->mFloaterDirectory && self->mFloaterDirectory->mPanelPlacep)
		{
			self->mFloaterDirectory->mPanelPlacep->setVisible(TRUE);
			self->mFloaterDirectory->mPanelPlacep->setParcelID(id);
			self->mFloaterDirectory->mPanelPlacep->sendParcelInfoRequest();
		}
		break;
	default:
		{
			llwarns << "Unknown event type!" << llendl;
		}
		break;
	}
}


void LLPanelDirBrowser::showEvent(const U32 event_id)
{
	// Start with everyone invisible
	if (mFloaterDirectory)
	{
		if (mFloaterDirectory->mPanelAvatarp) mFloaterDirectory->mPanelAvatarp->setVisible(FALSE);
		if (mFloaterDirectory->mPanelGroupp) mFloaterDirectory->mPanelGroupp->setVisible(FALSE);
		if (mFloaterDirectory->mPanelGroupHolderp) mFloaterDirectory->mPanelGroupHolderp->setVisible(FALSE);
		if (mFloaterDirectory->mPanelPlacep) mFloaterDirectory->mPanelPlacep->setVisible(FALSE);
		if (mFloaterDirectory->mPanelPlaceSmallp) mFloaterDirectory->mPanelPlaceSmallp->setVisible(FALSE);
		if (mFloaterDirectory->mPanelClassifiedp) mFloaterDirectory->mPanelClassifiedp->setVisible(FALSE);
		if (mFloaterDirectory->mPanelEventp)
		{
			mFloaterDirectory->mPanelEventp->setVisible(TRUE);
			mFloaterDirectory->mPanelEventp->setEventID(event_id);
		}
	}
}

void LLPanelDirBrowser::processDirPeopleReply(LLMessageSystem *msg, void**)
{
	LLUUID query_id;
	char   first_name[DB_FIRST_NAME_BUF_SIZE];	/* Flawfinder: ignore */
	char   last_name[DB_LAST_NAME_BUF_SIZE];	/* Flawfinder: ignore */
	LLUUID agent_id;

	msg->getUUIDFast(_PREHASH_QueryData,_PREHASH_QueryID, query_id);

	LLPanelDirBrowser* self;
	self = gDirBrowserInstances.getIfThere(query_id);
	if (!self) 
	{
		// data from an old query
		return;
	}

	self->mHaveSearchResults = TRUE;

	LLCtrlListInterface *list = self->childGetListInterface("results");
	if (!list) return;

	if (!list->getCanSelect())
	{
		list->operateOnAll(LLCtrlListInterface::OP_DELETE);
		self->mResultsContents = LLSD();
	}

	S32 rows = msg->getNumberOfBlocksFast(_PREHASH_QueryReplies);
	self->mResultsReceived += rows;

	rows = self->showNextButton(rows);

	LLString online_type = llformat("%d", ONLINE_CODE);
	LLString offline_type = llformat("%d", OFFLINE_CODE);

	for (S32 i = 0; i < rows; i++)
	{			
		msg->getStringFast(_PREHASH_QueryReplies,_PREHASH_FirstName, DB_FIRST_NAME_BUF_SIZE, first_name, i);
		msg->getStringFast(_PREHASH_QueryReplies,_PREHASH_LastName,	DB_LAST_NAME_BUF_SIZE, last_name, i);
		msg->getUUIDFast(  _PREHASH_QueryReplies,_PREHASH_AgentID, agent_id, i);
		// msg->getU8Fast(    _PREHASH_QueryReplies,_PREHASH_Online, online, i);
		// unused
		// msg->getStringFast(_PREHASH_QueryReplies,_PREHASH_Group, DB_GROUP_NAME_BUF_SIZE, group, i);
		// msg->getS32Fast(   _PREHASH_QueryReplies,_PREHASH_Reputation, reputation, i);

		if (agent_id.isNull())
		{
			continue;
		}

		LLSD content;

		LLSD row;
		row["id"] = agent_id;

		LLUUID image_id;
		// We don't show online status in the finder anymore,
		// so just use the 'offline' icon as the generic 'person' icon
		image_id.set( gViewerArt.getString("icon_avatar_offline.tga") );
		row["columns"][0]["column"] = "icon";
		row["columns"][0]["type"] = "icon";
		row["columns"][0]["value"] = image_id;

		content["type"] = OFFLINE_CODE;

		LLString fullname = LLString(first_name) + " " + LLString(last_name);
		row["columns"][1]["column"] = "name";
		row["columns"][1]["value"] = fullname;
		row["columns"][1]["font"] = "SANSSERIF";

		content["name"] = fullname;

		list->addElement(row);
		self->mResultsContents[agent_id.asString()] = content;
	}

	list->sortByColumn(self->mCurrentSortColumn, self->mCurrentSortAscending);
	self->updateResultCount();

	// Poke the result received timer
	self->mLastResultTimer.reset();
	self->mDidAutoSelect = FALSE;
}


void LLPanelDirBrowser::processDirPlacesReply(LLMessageSystem* msg, void**)
{
	LLUUID	agent_id;
	LLUUID	query_id;
	LLUUID	parcel_id;
	char	name[MAX_STRING];		/*Flawfinder: ignore*/
	BOOL	is_for_sale;
	BOOL	is_auction;
	F32		dwell;

	msg->getUUID("AgentData", "AgentID", agent_id);
	msg->getUUID("QueryData", "QueryID", query_id );

	LLPanelDirBrowser* self;
	self = gDirBrowserInstances.getIfThere(query_id);
	if (!self) 
	{
		// data from an old query
		return;
	}

	self->mHaveSearchResults = TRUE;

	LLCtrlListInterface *list = self->childGetListInterface("results");
	if (!list) return;

	if (!list->getCanSelect())
	{
		list->operateOnAll(LLCtrlListInterface::OP_DELETE);
		self->mResultsContents = LLSD();
	}

	S32 i;
	S32 count = msg->getNumberOfBlocks("QueryReplies");
	self->mResultsReceived += count;

	count = self->showNextButton(count);

	for (i = 0; i < count ; i++)
	{
		msg->getUUID("QueryReplies", "ParcelID", parcel_id, i);
		msg->getString("QueryReplies", "Name", MAX_STRING, name, i);
		msg->getBOOL("QueryReplies", "ForSale", is_for_sale, i);
		msg->getBOOL("QueryReplies", "Auction", is_auction, i);
		msg->getF32("QueryReplies", "Dwell", dwell, i);
		
		if (parcel_id.isNull())
		{
			continue;
		}

		LLSD content;
		S32 type;

		LLSD row = self->createLandSale(parcel_id, is_auction, is_for_sale,  name, &type);

		content["type"] = type;
		content["name"] = name;

		LLString buffer = llformat("%.0f", (F64)dwell);
		row["columns"][3]["column"] = "dwell";
		row["columns"][3]["value"] = buffer;
		row["columns"][3]["font"] = "SANSSERIFSMALL";

		list->addElement(row);
		self->mResultsContents[parcel_id.asString()] = content;
	}

	list->sortByColumn(self->mCurrentSortColumn, self->mCurrentSortAscending);
	self->updateResultCount();

	// Poke the result received timer
	self->mLastResultTimer.reset();
	self->mDidAutoSelect = FALSE;
}



void LLPanelDirBrowser::processDirPopularReply(LLMessageSystem *msg, void**)
{
	LLUUID	agent_id;
	LLUUID	query_id;
	LLUUID	parcel_id;
	char	name[MAX_STRING];		/*Flawfinder: ignore*/
	F32		dwell;

	msg->getUUID("AgentData", "AgentID", agent_id);
	msg->getUUID("QueryData", "QueryID", query_id );

	LLPanelDirBrowser* self;
	self = gDirBrowserInstances.getIfThere(query_id);
	if (!self) 
	{
		// data from an old query
		return;
	}

	self->mHaveSearchResults = TRUE;

	LLCtrlListInterface *list = self->childGetListInterface("results");
	if (!list) return;

	if (!list->getCanSelect())
	{
		list->operateOnAll(LLCtrlListInterface::OP_DELETE);
		self->mResultsContents = LLSD();
	}

	S32 i;
	S32 count = msg->getNumberOfBlocks("QueryReplies");
	self->mResultsReceived += count;
	for (i = 0; i < count; i++)
	{
		msg->getUUID(	"QueryReplies", "ParcelID", parcel_id, i);
		msg->getString(	"QueryReplies", "Name", MAX_STRING, name, i);
		msg->getF32(	"QueryReplies", "Dwell", dwell, i);

		if (parcel_id.isNull())
		{
			continue;
		}

		LLSD content;
		content["type"] = POPULAR_CODE;
		content["name"] = name;

		LLSD row;
		row["id"] = parcel_id;

		LLUUID image_id;
		image_id.set( gViewerArt.getString("icon_popular.tga") );
		row["columns"][0]["column"] = "icon";
		row["columns"][0]["type"] = "icon";
		row["columns"][0]["value"] = image_id;

		row["columns"][1]["column"] = "name";
		row["columns"][1]["value"] = name;
		row["columns"][1]["font"] = "SANSSERIF";

		LLString buffer = llformat("%.0f", dwell);
		row["columns"][2]["column"] = "dwell";
		row["columns"][2]["value"] = buffer;
		row["columns"][2]["font"] = "SANSSERIFSMALL";

		list->addElement(row);
		self->mResultsContents[parcel_id.asString()] = content;
	}

	list->sortByColumn(self->mCurrentSortColumn, self->mCurrentSortAscending);
	self->updateResultCount();

	// Poke the result received timer
	self->mLastResultTimer.reset();
	self->mDidAutoSelect = FALSE;
}


void LLPanelDirBrowser::processDirEventsReply(LLMessageSystem* msg, void**)
{
	LLUUID	agent_id;
	LLUUID	query_id;
	LLUUID	owner_id;
	char	name[MAX_STRING];			/*Flawfinder: ignore*/
	char	date[MAX_STRING];		/*Flawfinder: ignore*/
	BOOL	show_mature = gSavedSettings.getBOOL("ShowMatureEvents");

	msg->getUUID("AgentData", "AgentID", agent_id);
	msg->getUUID("QueryData", "QueryID", query_id );

	LLPanelDirBrowser* self;
	self = gDirBrowserInstances.getIfThere(query_id);
	if (!self)
	{
		return;
	}

	self->mHaveSearchResults = TRUE;

	LLCtrlListInterface *list = self->childGetListInterface("results");
	if (!list) return;

	if (!list->getCanSelect())
	{
		list->operateOnAll(LLCtrlListInterface::OP_DELETE);
		self->mResultsContents = LLSD();
	}

	S32 rows = msg->getNumberOfBlocks("QueryReplies");
	self->mResultsReceived += rows;

	rows = self->showNextButton(rows);

	for (S32 i = 0; i < rows; i++)
	{
		U32 event_id;
		U32 unix_time;
		U32 event_flags;

		msg->getUUID("QueryReplies", "OwnerID", owner_id, i);
		msg->getString("QueryReplies", "Name", MAX_STRING, name, i);
		msg->getU32("QueryReplies", "EventID", event_id, i);
		msg->getString("QueryReplies", "Date", MAX_STRING, date, i);
		msg->getU32("QueryReplies", "UnixTime", unix_time, i);
		msg->getU32("QueryReplies", "EventFlags", event_flags, i);
	
		// Skip empty events
		if (owner_id.isNull())
		{
			//RN: should this check event_id instead?
			llwarns << "skipped event due to owner_id null, event_id " << event_id << llendl;
			continue;
		}
		// Skip mature events if not showing
		if (!show_mature
			&& (event_flags & EVENT_FLAG_MATURE))
		{
			llwarns << "Skipped due to maturity, event_id " << event_id << llendl;
			continue;
		}

		LLSD content;

		content["type"] = EVENT_CODE;
		content["name"] = name;
		content["event_id"] = (S32)event_id;

		LLSD row;
		row["id"] = llformat("%u", event_id);

		// Column 0 - event icon
		LLUUID image_id;
		if (event_flags & EVENT_FLAG_MATURE)
		{
			image_id.set( gViewerArt.getString("icon_event_mature.tga") );
			row["columns"][0]["column"] = "icon";
			row["columns"][0]["type"] = "icon";
			row["columns"][0]["value"] = image_id;
		}
		else
		{
			image_id.set( gViewerArt.getString("icon_event.tga") );
			row["columns"][0]["column"] = "icon";
			row["columns"][0]["type"] = "icon";
			row["columns"][0]["value"] = image_id;
		}

		row["columns"][1]["column"] = "name";
		row["columns"][1]["value"] = name;
		row["columns"][1]["font"] = "SANSSERIF";

		row["columns"][2]["column"] = "date";
		row["columns"][2]["value"] = date;
		row["columns"][2]["font"] = "SANSSERIFSMALL";

		row["columns"][3]["column"] = "time";
		row["columns"][3]["value"] = llformat("%u", unix_time);
		row["columns"][3]["font"] = "SANSSERIFSMALL";

		list->addElement(row, ADD_SORTED);

		LLString id_str = llformat("%u", event_id);
		self->mResultsContents[id_str] = content;
	}

	list->sortByColumn(self->mCurrentSortColumn, self->mCurrentSortAscending);
	self->updateResultCount();

	// Poke the result received timer
	self->mLastResultTimer.reset();
	self->mDidAutoSelect = FALSE;
}


// static
void LLPanelDirBrowser::processDirGroupsReply(LLMessageSystem* msg, void**)
{
	S32		i;
	
	LLUUID	query_id;
	LLUUID	group_id;
	char	group_name[DB_GROUP_NAME_BUF_SIZE];		/*Flawfinder: ignore*/
	S32     members;
	F32     search_order;

	msg->getUUIDFast(_PREHASH_QueryData,_PREHASH_QueryID, query_id );

	LLPanelDirBrowser* self;
	self = gDirBrowserInstances.getIfThere(query_id);
	if (!self)
	{
		return;
	}

	self->mHaveSearchResults = TRUE;

	LLCtrlListInterface *list = self->childGetListInterface("results");
	if (!list) return;

	if (!list->getCanSelect())
	{
		list->operateOnAll(LLCtrlListInterface::OP_DELETE);
		self->mResultsContents = LLSD();
	}

	S32 rows = msg->getNumberOfBlocksFast(_PREHASH_QueryReplies);
	self->mResultsReceived += rows;

	rows = self->showNextButton(rows);

	for (i = 0; i < rows; i++)
	{
		msg->getUUIDFast(_PREHASH_QueryReplies, _PREHASH_GroupID,		group_id, i );
		msg->getStringFast(_PREHASH_QueryReplies, _PREHASH_GroupName,	DB_GROUP_NAME_BUF_SIZE,		group_name,		i);
		msg->getS32Fast(_PREHASH_QueryReplies, _PREHASH_Members,		members, i );
		msg->getF32Fast(_PREHASH_QueryReplies, _PREHASH_SearchOrder,	search_order, i );
		
		if (group_id.isNull())
		{
			continue;
		}

		LLSD content;
		content["type"] = GROUP_CODE;
		content["name"] = group_name;

		LLSD row;
		row["id"] = group_id;

		LLUUID image_id;
		image_id.set( gViewerArt.getString("icon_group.tga") );
		row["columns"][0]["column"] = "icon";
		row["columns"][0]["type"] = "icon";
		row["columns"][0]["value"] = image_id;

		row["columns"][1]["column"] = "name";
		row["columns"][1]["value"] = group_name;
		row["columns"][1]["font"] = "SANSSERIF";

		row["columns"][2]["column"] = "members";
		row["columns"][2]["value"] = members;
		row["columns"][2]["font"] = "SANSSERIFSMALL";

		row["columns"][3]["column"] = "score";
		row["columns"][3]["value"] = search_order;

		list->addElement(row);
		self->mResultsContents[group_id.asString()] = content;
	}
	list->sortByColumn(self->mCurrentSortColumn, self->mCurrentSortAscending);
	self->updateResultCount();

	// Poke the result received timer
	self->mLastResultTimer.reset();
	self->mDidAutoSelect = FALSE;
}


// static
void LLPanelDirBrowser::processDirClassifiedReply(LLMessageSystem* msg, void**)
{
	S32		i;
	S32		num_new_rows;

	LLUUID	agent_id;
	LLUUID	query_id;

	msg->getUUID("AgentData", "AgentID", agent_id);
	if (agent_id != gAgent.getID())
	{
		llwarns << "Message for wrong agent " << agent_id
			<< " in processDirClassifiedReply" << llendl;
		return;
	}

	msg->getUUID("QueryData", "QueryID", query_id);
	LLPanelDirBrowser* self = gDirBrowserInstances.getIfThere(query_id);
	if (!self)
	{
		return;
	}

	self->mHaveSearchResults = TRUE;

	LLCtrlListInterface *list = self->childGetListInterface("results");
	if (!list) return;

	if (!list->getCanSelect())
	{
		list->operateOnAll(LLCtrlListInterface::OP_DELETE);
		self->mResultsContents = LLSD();
	}

	num_new_rows = msg->getNumberOfBlocksFast(_PREHASH_QueryReplies);
	self->mResultsReceived += num_new_rows;

	num_new_rows = self->showNextButton(num_new_rows);
	for (i = 0; i < num_new_rows; i++)
	{
		LLUUID classified_id;
		char name[DB_PARCEL_NAME_SIZE];		/*Flawfinder: ignore*/
		U32 creation_date = 0;	// unix timestamp
		U32 expiration_date = 0;	// future use
		S32 price_for_listing = 0;
		msg->getUUID("QueryReplies", "ClassifiedID", classified_id, i);
		msg->getString("QueryReplies", "Name", DB_PARCEL_NAME_SIZE, name, i);
		msg->getU32("QueryReplies","CreationDate",creation_date,i);
		msg->getU32("QueryReplies","ExpirationDate",expiration_date,i);
		msg->getS32("QueryReplies","PriceForListing",price_for_listing,i);

		if ( classified_id.notNull() )
		{
			self->addClassified(list, classified_id, name, creation_date, price_for_listing);

			LLSD content;
			content["type"] = CLASSIFIED_CODE;
			content["name"] = name;
			self->mResultsContents[classified_id.asString()] = content;
		}
	}
	// The server does the initial sort, by price paid per listing and date. JC
	self->updateResultCount();

	// Poke the result received timer
	self->mLastResultTimer.reset();
	self->mDidAutoSelect = FALSE;
}

void LLPanelDirBrowser::processDirLandReply(LLMessageSystem *msg, void**)
{
	LLUUID	agent_id;
	LLUUID	query_id;
	LLUUID	parcel_id;
	char	name[MAX_STRING];		/*Flawfinder: ignore*/
	BOOL	auction;
	BOOL	for_sale;
	S32		sale_price;
	S32		actual_area;

	msg->getUUID("AgentData", "AgentID", agent_id);
	msg->getUUID("QueryData", "QueryID", query_id );

	LLPanelDirBrowser* browser;
	browser = gDirBrowserInstances.getIfThere(query_id);
	if (!browser) 
	{
		// data from an old query
		return;
	}

	// Only handled by LLPanelDirLand
	LLPanelDirLand* self = (LLPanelDirLand*)browser;

	self->mHaveSearchResults = TRUE;

	LLCtrlListInterface *list = self->childGetListInterface("results");
	if (!list) return;

	if (!list->getCanSelect())
	{
		list->operateOnAll(LLCtrlListInterface::OP_DELETE);
		self->mResultsContents = LLSD();
	}

	BOOL use_price = gSavedSettings.getBOOL("FindLandPrice");
	S32 limit_price = self->childGetValue("priceedit").asInteger();

	BOOL use_area = gSavedSettings.getBOOL("FindLandArea");
	S32 limit_area = self->childGetValue("areaedit").asInteger();

	S32 i;
	S32 count = msg->getNumberOfBlocks("QueryReplies");
	self->mResultsReceived += count;
	
	S32 non_auction_count = 0;
	for (i = 0; i < count; i++)
	{
		msg->getUUID(	"QueryReplies", "ParcelID", parcel_id, i);
		msg->getString(	"QueryReplies", "Name", MAX_STRING, name, i);
		msg->getBOOL(	"QueryReplies", "Auction", auction, i);
		msg->getBOOL(	"QueryReplies", "ForSale", for_sale, i);
		msg->getS32(	"QueryReplies", "SalePrice", sale_price, i);
		msg->getS32(	"QueryReplies", "ActualArea", actual_area, i);
		
		if (parcel_id.isNull()) continue;

		if (use_price && (sale_price > limit_price)) continue;

		if (use_area && (actual_area < limit_area)) continue;

		LLSD content;
		S32 type;

		LLSD row = self->createLandSale(parcel_id, auction, for_sale,  name, &type);

		content["type"] = type;
		content["name"] = name;

		LLString buffer = "Auction";
		if (!auction)
		{
			buffer = llformat("%d", sale_price);
			non_auction_count++;
		}
		row["columns"][3]["column"] = "price";
		row["columns"][3]["value"] = buffer;
		row["columns"][3]["font"] = "SANSSERIFSMALL";

		buffer = llformat("%d", actual_area);
		row["columns"][4]["column"] = "area";
		row["columns"][4]["value"] = buffer;
		row["columns"][4]["font"] = "SANSSERIFSMALL";

		if (!auction)
		{
			F32 price_per_meter;
			if (actual_area > 0)
			{
				price_per_meter = (F32)sale_price / (F32)actual_area;
			}
			else
			{
				price_per_meter = 0.f;
			}
			// Prices are usually L$1 - L$10 / meter
			buffer = llformat("%.1f", price_per_meter);
			row["columns"][5]["column"] = "per_meter";
			row["columns"][5]["value"] = buffer;
			row["columns"][5]["font"] = "SANSSERIFSMALL";
		}
		else
		{
			// Auctions start at L$1 per meter
			row["columns"][5]["column"] = "per_meter";
			row["columns"][5]["value"] = "1.0";
			row["columns"][5]["font"] = "SANSSERIFSMALL";
		}

		list->addElement(row);
		self->mResultsContents[parcel_id.asString()] = content;
	}

	// All auction results are shown on the first page
	// But they don't count towards the 100 / page limit
	// So figure out the next button here, when we know how many aren't auctions
	count = self->showNextButton(non_auction_count);

	// Empty string will sort by current sort options.
	list->sortByColumn("",FALSE);
	self->updateResultCount();

	// Poke the result received timer
	self->mLastResultTimer.reset();
	self->mDidAutoSelect = FALSE;
}

void LLPanelDirBrowser::addClassified(LLCtrlListInterface *list, const LLUUID& pick_id, const char* name, const U32 creation_date, const S32 price_for_listing)
{
	LLString type = llformat("%d", CLASSIFIED_CODE);

	LLSD row;
	row["id"] = pick_id;

	LLUUID image_id;
	image_id.set( gViewerArt.getString("icon_top_pick.tga") );
	row["columns"][0]["column"] = "icon";
	row["columns"][0]["type"] = "icon";
	row["columns"][0]["value"] = image_id;

	row["columns"][1]["column"] = "name";
	row["columns"][1]["value"] = name;
	row["columns"][1]["font"] = "SANSSERIF";

	row["columns"][2]["column"] = "price";
	row["columns"][2]["value"] = price_for_listing;
	row["columns"][2]["font"] = "SANSSERIFSMALL";

	list->addElement(row);
}

LLSD LLPanelDirBrowser::createLandSale(const LLUUID& parcel_id, BOOL is_auction, BOOL is_for_sale,  const LLString& name, S32 *type)
{
	LLSD row;
	row["id"] = parcel_id;
	LLUUID image_id;

	// Icon and type
	if(is_auction)
	{
		image_id.set( gViewerArt.getString("icon_auction.tga") );
		row["columns"][0]["column"] = "icon";
		row["columns"][0]["type"] = "icon";
		row["columns"][0]["value"] = image_id;

		*type = AUCTION_CODE;
	}
	else if (is_for_sale)
	{
		image_id.set( gViewerArt.getString("icon_for_sale.tga") );
		row["columns"][0]["column"] = "icon";
		row["columns"][0]["type"] = "icon";
		row["columns"][0]["value"] = image_id;

		*type = FOR_SALE_CODE;
	}
	else
	{
		image_id.set( gViewerArt.getString("icon_place.tga") );
		row["columns"][0]["column"] = "icon";
		row["columns"][0]["type"] = "icon";
		row["columns"][0]["value"] = image_id;

		*type = PLACE_CODE;
	}

	row["columns"][2]["column"] = "name";
	row["columns"][2]["value"] = name;
	row["columns"][2]["font"] = "SANSSERIF";

	return row;
}

void LLPanelDirBrowser::newClassified()
{
	LLCtrlListInterface *list = childGetListInterface("results");
	if (!list) return;

	if (mFloaterDirectory->mPanelClassifiedp)
	{
		// Clear the panel on the right
		mFloaterDirectory->mPanelClassifiedp->reset();

		// Set up the classified with the info we've created
		// and a sane default position.
		mFloaterDirectory->mPanelClassifiedp->initNewClassified();

		// We need the ID to select in the list.
		LLUUID classified_id = mFloaterDirectory->mPanelClassifiedp->getClassifiedID();

		// Put it in the list on the left
		addClassified(list, classified_id, mFloaterDirectory->mPanelClassifiedp->getClassifiedName().c_str(),0,0);

		// Select it.
		list->setCurrentByID(classified_id);

		// Make the right panel visible (should already be)
		mFloaterDirectory->mPanelClassifiedp->setVisible(TRUE);
	}
}

void LLPanelDirBrowser::renameClassified(const LLUUID& classified_id, const char* name)
{
	// TomY What, really?
	/*LLScrollListItem* row;
	for (row = mResultsList->getFirstData(); row; row = mResultsList->getNextData())
	{
		if (row->getUUID() == classified_id)
		{
			const LLScrollListCell* column;
			LLScrollListText* text;

			// icon
			// type
			column = row->getColumn(2);	// name (visible)
			text = (LLScrollListText*)column;
			text->setText(name);

			column = row->getColumn(3);	// name (invisible)
			text = (LLScrollListText*)column;
			text->setText(name);
		}
	}*/
}


void LLPanelDirBrowser::setupNewSearch()
{
	LLCtrlListInterface *list = childGetListInterface("results");
	if (!list) return;

	gDirBrowserInstances.removeData(mSearchID);
	// Make a new query ID
	mSearchID.generate();

	gDirBrowserInstances.addData(mSearchID, this);

	// ready the list for results
	list->operateOnAll(LLCtrlListInterface::OP_DELETE);
	list->addSimpleElement("Searching...");
	childDisable("results");

	mResultsReceived = 0;
	mHaveSearchResults = FALSE;

	// Set all panels to be invisible
	if (mFloaterDirectory->mPanelAvatarp) mFloaterDirectory->mPanelAvatarp->setVisible(FALSE);
	if (mFloaterDirectory->mPanelEventp) mFloaterDirectory->mPanelEventp->setVisible(FALSE);
	if (mFloaterDirectory->mPanelGroupp) mFloaterDirectory->mPanelGroupp->setVisible(FALSE);
	if (mFloaterDirectory->mPanelGroupHolderp) mFloaterDirectory->mPanelGroupHolderp->setVisible(FALSE);
	if (mFloaterDirectory->mPanelPlacep) mFloaterDirectory->mPanelPlacep->setVisible(FALSE);
	if (mFloaterDirectory->mPanelPlaceSmallp) mFloaterDirectory->mPanelPlaceSmallp->setVisible(FALSE);
	if (mFloaterDirectory->mPanelClassifiedp) mFloaterDirectory->mPanelClassifiedp->setVisible(FALSE);

	updateResultCount();
}


// static
void LLPanelDirBrowser::onClickSearchCore(void* userdata)
{
	LLPanelDirBrowser* self = (LLPanelDirBrowser*)userdata;
	if (!self) return;

	self->resetSearchStart();
	self->performQuery();
}


// static
void LLPanelDirBrowser::sendDirFindQuery(
	LLMessageSystem* msg,
	const LLUUID& query_id,
	const LLString& text,
	U32 flags,
	S32 query_start)
{
	msg->newMessage("DirFindQuery");
	msg->nextBlock("AgentData");
	msg->addUUID("AgentID", gAgent.getID());
	msg->addUUID("SessionID", gAgent.getSessionID());
	msg->nextBlock("QueryData");
	msg->addUUID("QueryID", query_id);
	msg->addString("QueryText", text);
	msg->addU32("QueryFlags", flags);
	msg->addS32("QueryStart", query_start);
	gAgent.sendReliableMessage();
}


void LLPanelDirBrowser::addHelpText(const char* text)
{
	LLCtrlListInterface *list = childGetListInterface("results");
	if (!list) return;

	list->addSimpleElement(text);
	childDisable("results");
}


BOOL enable_never(void*)
{
	return FALSE;
}

void LLPanelDirBrowser::onKeystrokeName(LLLineEditor* line, void* data)
{
	LLPanelDirBrowser *self = (LLPanelDirBrowser*)data;
	if (line->getLength() >= (S32)self->mMinSearchChars)
	{
		self->setDefaultBtn( "Search" );
		self->childEnable("Search");
	}
	else
	{
		self->setDefaultBtn();
		self->childDisable("Search");
	}
}

void LLPanelDirBrowser::onVisibilityChange(BOOL curVisibilityIn)
{
	if (curVisibilityIn)
	{
		onCommitList(NULL, this);
	}
}

S32 LLPanelDirBrowser::showNextButton(S32 rows)
{
	// HACK: This hack doesn't work for llpaneldirfind (ALL) 
	// because other data is being returned as well.
	if ( getName() != "all_panel")
	{
		// HACK: The (mResultsPerPage)+1th entry indicates there are 'more'
		bool show_next = (mResultsReceived > mResultsPerPage);
		childSetVisible("Next >", show_next);
		if (show_next)
		{
			rows -= (mResultsReceived - mResultsPerPage);
		}
	}
	else
	{
		// Hide page buttons
		childHide("Next >");
		childHide("< Prev");
	}
	return rows;
}
