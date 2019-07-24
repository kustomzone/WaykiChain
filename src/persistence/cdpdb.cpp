// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "cdpdb.h"

#include "entities/id.h"
#include "main.h"

#include <cstdint>

bool CCdpMemCache::LoadAllCdpFromDB() {
    assert(pAccess != nullptr);
    map<std::pair<string, string>, CUserCDP> rawCdps;

    if (!pAccess->GetAllElements(dbk::CDP, rawCdps)) {
        // TODO: log
        return false;
    }

    for (const auto & item: rawCdps) {
        static uint8_t valid = 1; // 0: invalid; 1: valid

        cdps.emplace(item.second, valid);
        total_staked_bcoins += item.second.total_staked_bcoins;
        total_owed_scoins += item.second.total_owed_scoins;
    }

    return true;
}

void CCdpMemCache::SetBase(CCdpMemCache *pBaseIn) {
    assert(pBaseIn != nullptr);
    pBase = pBaseIn;
}

void CCdpMemCache::Flush() {
    if (pBase != nullptr) {
        pBase->BatchWrite(cdps);
        cdps.clear();
    }
}

bool CCdpMemCache::SaveCdp(const CUserCDP &userCdp) {
    static uint8_t valid = 1;  // 0: invalid; 1: valid
    cdps.emplace(userCdp, valid);

    total_staked_bcoins += userCdp.total_staked_bcoins;
    total_owed_scoins += userCdp.total_owed_scoins;

    return true;
}

bool CCdpMemCache::EraseCdp(const CUserCDP &userCdp) {
    static uint8_t invalid = 0;  // 0: invalid; 1: valid

    cdps[userCdp] = invalid;
    total_staked_bcoins -= userCdp.total_staked_bcoins;
    total_owed_scoins -= userCdp.total_owed_scoins;

    return true;
}


uint64_t CCdpMemCache::GetGlobalCollateralRatio(const uint64_t bcoinMedianPrice) const {
    // If total owed scoins equal to zero, the global collateral ratio becomes infinite.
    return (total_owed_scoins == 0) ? UINT64_MAX : total_staked_bcoins * bcoinMedianPrice * kPercentBoost / total_owed_scoins;
}

uint64_t CCdpMemCache::GetGlobalCollateral() const {
    return total_staked_bcoins;
}

bool CCdpMemCache::GetCdpListByCollateralRatio(const uint64_t collateralRatio, const uint64_t bcoinMedianPrice,
                                         set<CUserCDP> &userCdps) {
    return GetCdpList(collateralRatio * bcoinMedianPrice, userCdps);
}

bool CCdpMemCache::GetCdpList(const double ratio, set<CUserCDP> &expiredCdps, set<CUserCDP> &userCdps) {
    static CRegID regId(std::numeric_limits<uint32_t>::max(), std::numeric_limits<uint16_t>::max());
    static uint256 txid;
    static CUserCDP cdp(regId, txid);
    cdp.collateralRatioBase = ratio;
    cdp.ownerRegId          = regId;

    auto boundary = cdps.upper_bound(cdp);
    if (boundary != cdps.end()) {
        for (auto iter = cdps.begin(); iter != boundary; ++ iter) {
            if (db_util::IsEmpty(iter->second)) {
                expiredCdps.insert(iter->first);
            } else if (expiredCdps.count(iter->first) || userCdps.count(iter->first)) {
                // TODO: log
                continue;
            } else {
                // Got a valid cdp
                userCdps.insert(iter->first);
            }
        }
    }

    if (pBase != nullptr) {
        return pBase->GetCdpList(ratio, expiredCdps, userCdps);
    }

    return true;
}

bool CCdpMemCache::GetCdpList(const double ratio, set<CUserCDP> &userCdps) {
    set<CUserCDP> expiredCdps;
    if (!GetCdpList(ratio, expiredCdps, userCdps)) {
        // TODO: log
        return false;
    }

    return true;
}

void CCdpMemCache::BatchWrite(const map<CUserCDP, uint8_t> &cdpsIn) {
    // If the value is empty, delete it from cache.
    for (const auto &item : cdpsIn) {
        if (db_util::IsEmpty(item.second)) {
            cdps.erase(item.first);
        } else {
            cdps[item.first] = item.second;
        }
    }
}

bool CCdpDBCache::StakeBcoinsToCdp(const int32_t blockHeight, const uint64_t bcoinsToStake, const uint64_t mintedScoins,
                                    CUserCDP &cdp) {
    // 1. erase the old cdp in memory cache
    cdpMemCache.EraseCdp(cdp);

    // 2. update cdp's properties before saving
    cdp.blockHeight = blockHeight;
    cdp.total_staked_bcoins += bcoinsToStake;
    cdp.total_owed_scoins += mintedScoins;
    cdp.collateralRatioBase = double(cdp.total_staked_bcoins) / cdp.total_owed_scoins;
    if (!SaveCdp(cdp)) {
        return ERRORMSG("CCdpDBCache::StakeBcoinsToCdp : SetData failed.");
    }

    // 3. save the new cdp in memory cache
    cdpMemCache.SaveCdp(cdp);

    return true;
}

bool CCdpDBCache::GetCdpList(const CRegID &regId, vector<CUserCDP> &cdpList) {
    set<uint256> cdpTxids;
    if (!regId2CdpCache.GetData(regId.ToRawString(), cdpTxids)) {
        return false;
    }

    CUserCDP userCdp;
    for (const auto &item : cdpTxids) {
        if (!cdpCache.GetData(item, userCdp)) {
            return false;
        }

        cdpList.push_back(userCdp);
    }

    return true;
}

bool CCdpDBCache::GetCdp(CUserCDP &cdp) {
    if (!cdpCache.GetData(cdp.cdpTxId, cdp))
        return false;

    return true;
}

// Attention: update cdpCache and regId2CdpCache synchronously.
bool CCdpDBCache::SaveCdp(CUserCDP &cdp) {
    set<uint256> cdpTxids;
    regId2CdpCache.GetData(cdp.ownerRegId.ToRawString(), cdpTxids);
    cdpTxIds.insert(cdp.cdpTxId);

    return cdpCache.SetData(cdp.cdpTxId, cdp) && regId2CdpCache.SetData(cdp.cdpTxId, cdpTxids);
}

bool CCdpDBCache::EraseCdp(const CUserCDP &cdp) {
    set<uint256> cdpTxids;
    regId2CdpCache.GetData(cdp.ownerRegId.ToRawString(), cdpTxids);
    cdpTxIds.erase(cdp.cdpTxId);

    // If cdpTxids is empty, regId2CdpCache will erase the key automatically.
    return cdpCache.EraseData(cdp.cdpTxId)) && regId2CdpCache.SetData(cdp.cdpTxId, cdpTxids);
}

// global collateral ratio floor check
bool CCdpDBCache::CheckGlobalCollateralRatioFloorReached(const uint64_t &bcoinMedianPrice,
                                                         const uint64_t &kGlobalCollateralRatioLimit) {
    bool floorRatioReached = cdpMemCache.GetGlobalCollateralRatio(bcoinMedianPrice) < kGlobalCollateralRatioLimit;
    return floorRatioReached;
}

// global collateral amount ceiling check
bool CCdpDBCache::CheckGlobalCollateralCeilingReached(const uint64_t &newBcoinsToStake,
                                                      const uint64_t &kGlobalCollateralCeiling) {
    bool ceilingAmountReached = (newBcoinsToStake + cdpMemCache.GetGlobalCollateral()) >
                                    kGlobalCollateralCeiling * COIN;

    return ceilingAmountReached;
}

bool CCdpDBCache::Flush() {
    cdpCache.Flush();
    regId2CdpCache.Flush();
    cdpMemCache.Flush();

    return true;
}

uint32_t CCdpDBCache::GetCacheSize() const { return cdpCache.GetCacheSize() + regId2CdpCache.GetCacheSize(); }