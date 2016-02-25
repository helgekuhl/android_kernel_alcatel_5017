














#include "precomp.h"


static UINT_8
rlmObssChnlLevelIn2G4(P_BSS_INFO_T prBssInfo, UINT_8 ucPriChannel, ENUM_CHNL_EXT_T eExtend);

static UINT_8
rlmObssChnlLevelIn5G(P_BSS_INFO_T prBssInfo, UINT_8 ucPriChannel, ENUM_CHNL_EXT_T eExtend);


/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
UINT_8
rlmObssChnlLevel(P_BSS_INFO_T prBssInfo,
		 ENUM_BAND_T eBand, UINT_8 ucPriChannel, ENUM_CHNL_EXT_T eExtend)
{
	UINT_8 ucChannelLevel;

	ASSERT(prBssInfo);

	if (eBand == BAND_2G4) {
		ucChannelLevel = rlmObssChnlLevelIn2G4(prBssInfo, ucPriChannel, eExtend);

		/* (TBD) If concurrent networks permit different channel, extra
		 *       channel judgement should be added. Please refer to
		 *       previous version of this file.
		 */
	} else if (eBand == BAND_5G) {
		ucChannelLevel = rlmObssChnlLevelIn5G(prBssInfo, ucPriChannel, eExtend);

		/* (TBD) If concurrent networks permit different channel, extra
		 *       channel judgement should be added. Please refer to
		 *       previous version of this file.
		 */
	} else {
		ucChannelLevel = CHNL_LEVEL0;
	}

	return ucChannelLevel;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
static UINT_8
rlmObssChnlLevelIn2G4(P_BSS_INFO_T prBssInfo, UINT_8 ucPriChannel, ENUM_CHNL_EXT_T eExtend)
{
	UINT_8 i, ucChannelLevel;
	UINT_8 ucSecChannel, ucCenterChannel;
	UINT_8 ucAffectedChnl_L, ucAffectedChnl_H;

	ASSERT(prBssInfo);

	ucChannelLevel = CHNL_LEVEL2;

	/* Calculate center channel for 2.4G band */
	if (eExtend == CHNL_EXT_SCA) {
		ucCenterChannel = ucPriChannel + 2;
		ucSecChannel = ucPriChannel + 4;
	} else if (eExtend == CHNL_EXT_SCB) {
		ucCenterChannel = ucPriChannel - 2;
		ucSecChannel = ucPriChannel - 4;
	} else {
		return CHNL_LEVEL0;
	}
	ASSERT(ucCenterChannel >= 1 && ucCenterChannel <= 14);

	/* Calculated low/upper channels in affected freq range */
	ucAffectedChnl_L = (ucCenterChannel <= AFFECTED_CHNL_OFFSET) ?
	    1 : (ucCenterChannel - AFFECTED_CHNL_OFFSET);

	ucAffectedChnl_H = (ucCenterChannel >= (14 - AFFECTED_CHNL_OFFSET)) ?
	    14 : (ucCenterChannel + AFFECTED_CHNL_OFFSET);


	/* Check intolerant (Non-HT) channel list */
	ASSERT(prBssInfo->auc2G_NonHtChnlList[0] <= CHNL_LIST_SZ_2G);
	for (i = 1; i <= prBssInfo->auc2G_NonHtChnlList[0] && i <= CHNL_LIST_SZ_2G; i++) {
		if ((prBssInfo->auc2G_NonHtChnlList[i] >= ucAffectedChnl_L &&
		     prBssInfo->auc2G_NonHtChnlList[i] <= ucAffectedChnl_H) &&
		    prBssInfo->auc2G_NonHtChnlList[i] != ucPriChannel) {

			ucChannelLevel = CHNL_LEVEL0;
			goto L_2G4_level_end;
		}
	}

	/* Check 20M BW request channel list */
	ASSERT(prBssInfo->auc2G_20mReqChnlList[0] <= CHNL_LIST_SZ_2G);
	for (i = 1; i <= prBssInfo->auc2G_20mReqChnlList[0] && i <= CHNL_LIST_SZ_2G; i++) {
		if ((prBssInfo->auc2G_20mReqChnlList[i] >= ucAffectedChnl_L &&
		     prBssInfo->auc2G_20mReqChnlList[i] <= ucAffectedChnl_H)) {

			ucChannelLevel = CHNL_LEVEL0;
			goto L_2G4_level_end;
		}
	}

	/* Check 2.4G primary channel list */
	ASSERT(prBssInfo->auc2G_PriChnlList[0] <= CHNL_LIST_SZ_2G);
	for (i = 1; i <= prBssInfo->auc2G_PriChnlList[0] && i <= CHNL_LIST_SZ_2G; i++) {
		if ((prBssInfo->auc2G_PriChnlList[i] >= ucAffectedChnl_L &&
		     prBssInfo->auc2G_PriChnlList[i] <= ucAffectedChnl_H) &&
		    prBssInfo->auc2G_PriChnlList[i] != ucPriChannel) {

			ucChannelLevel = CHNL_LEVEL0;
			goto L_2G4_level_end;
		}
	}

	/* Check 2.4G secondary channel list */
	ASSERT(prBssInfo->auc2G_SecChnlList[0] <= CHNL_LIST_SZ_2G);
	for (i = 1; i <= prBssInfo->auc2G_SecChnlList[0] && i <= CHNL_LIST_SZ_2G; i++) {
		if ((prBssInfo->auc2G_SecChnlList[i] >= ucAffectedChnl_L &&
		     prBssInfo->auc2G_SecChnlList[i] <= ucAffectedChnl_H) &&
		    prBssInfo->auc2G_SecChnlList[i] != ucSecChannel) {

			ucChannelLevel = CHNL_LEVEL0;
			goto L_2G4_level_end;
		}
	}

L_2G4_level_end:

	return ucChannelLevel;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
static UINT_8
rlmObssChnlLevelIn5G(P_BSS_INFO_T prBssInfo, UINT_8 ucPriChannel, ENUM_CHNL_EXT_T eExtend)
{
	UINT_8 i, ucChannelLevel;
	UINT_8 ucSecChannel;

	ASSERT(prBssInfo);

	ucChannelLevel = CHNL_LEVEL2;

	/* Calculate center channel for 2.4G band */
	if (eExtend == CHNL_EXT_SCA) {
		ucSecChannel = ucPriChannel + 4;
	} else if (eExtend == CHNL_EXT_SCB) {
		ucSecChannel = ucPriChannel - 4;
	} else {
		return CHNL_LEVEL0;
	}
	ASSERT(ucSecChannel >= 36);

	/* Check 5G primary channel list */
	ASSERT(prBssInfo->auc5G_PriChnlList[0] <= CHNL_LIST_SZ_5G);
	for (i = 1; i <= prBssInfo->auc5G_PriChnlList[0] && i <= CHNL_LIST_SZ_5G; i++) {
		if (prBssInfo->auc5G_PriChnlList[i] == ucSecChannel) {

			ucChannelLevel = CHNL_LEVEL0;
			goto L_5G_level_end;
		} else if (prBssInfo->auc5G_PriChnlList[i] == ucPriChannel) {
			ucChannelLevel = CHNL_LEVEL1;
		}
	}

	/* Check non-HT channel list */
	ASSERT(prBssInfo->auc5G_NonHtChnlList[0] <= CHNL_LIST_SZ_5G);
	for (i = 1; i <= prBssInfo->auc5G_NonHtChnlList[0] && i <= CHNL_LIST_SZ_5G; i++) {
		if (prBssInfo->auc5G_NonHtChnlList[i] == ucSecChannel) {

			ucChannelLevel = CHNL_LEVEL0;
			goto L_5G_level_end;
		} else if (prBssInfo->auc5G_NonHtChnlList[i] == ucPriChannel) {
			ucChannelLevel = CHNL_LEVEL1;
		}
	}

	/* Check secondary channel list */
	ASSERT(prBssInfo->auc5G_SecChnlList[0] <= CHNL_LIST_SZ_5G);
	for (i = 1; i <= prBssInfo->auc5G_SecChnlList[0] && i <= CHNL_LIST_SZ_5G; i++) {
		if (prBssInfo->auc5G_SecChnlList[i] == ucPriChannel) {

			ucChannelLevel = CHNL_LEVEL0;
			goto L_5G_level_end;
		}
	}

L_5G_level_end:

	return ucChannelLevel;
}

/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
VOID rlmObssScanExemptionRsp(P_ADAPTER_T prAdapter, P_BSS_INFO_T prBssInfo, P_SW_RFB_T prSwRfb)
{
	P_MSDU_INFO_T prMsduInfo;
	P_ACTION_20_40_COEXIST_FRAME prTxFrame;

	/* To do: need an algorithm to do judgement. Now always reject request */

	prMsduInfo = (P_MSDU_INFO_T)
	    cnmMgtPktAlloc(prAdapter, PUBLIC_ACTION_MAX_LEN);
	if (prMsduInfo == NULL) {
		return;
	}

	DBGLOG(RLM, INFO, ("Send 20/40 coexistence rsp frame!\n"));

	prTxFrame = (P_ACTION_20_40_COEXIST_FRAME) prMsduInfo->prPacket;

	prTxFrame->u2FrameCtrl = MAC_FRAME_ACTION;
	COPY_MAC_ADDR(prTxFrame->aucDestAddr,
		      ((P_ACTION_20_40_COEXIST_FRAME) prSwRfb->pvHeader)->aucSrcAddr);
	COPY_MAC_ADDR(prTxFrame->aucSrcAddr, prBssInfo->aucOwnMacAddr);
	COPY_MAC_ADDR(prTxFrame->aucBSSID, prBssInfo->aucBSSID);

	prTxFrame->ucCategory = CATEGORY_PUBLIC_ACTION;
	prTxFrame->ucAction = ACTION_PUBLIC_20_40_COEXIST;

	/* To do: find correct algorithm */
	prTxFrame->rBssCoexist.ucId = ELEM_ID_20_40_BSS_COEXISTENCE;
	prTxFrame->rBssCoexist.ucLength = 1;
	prTxFrame->rBssCoexist.ucData = 0;

	ASSERT((WLAN_MAC_HEADER_LEN + 5) <= PUBLIC_ACTION_MAX_LEN);

	TX_SET_MMPDU(prAdapter,
		     prMsduInfo,
		     prBssInfo->ucBssIndex,
		     prSwRfb->ucStaRecIdx,
		     WLAN_MAC_MGMT_HEADER_LEN,
		     WLAN_MAC_MGMT_HEADER_HTC_LEN + 5, NULL, MSDU_RATE_MODE_AUTO);

	/* Send them to HW queue */
	nicTxEnqueueMsdu(prAdapter, prMsduInfo);
}
