// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2017-2019 The WaykiChain Developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include "coinrewardtx.h"

#include "main.h"

bool CCoinRewardTx::CheckTx(int32_t height, CCacheWrapper &cw, CValidationState &state) {
    // Only used in stable coin genesis.
    return height == (int32_t)SysCfg().GetStableCoinGenesisHeight() ? true : false;
}

bool CCoinRewardTx::ExecuteTx(int32_t height, int32_t index, CCacheWrapper &cw, CValidationState &state) {
    assert(txUid.type() == typeid(CPubKey));

    CAccount account;
    CRegID regId(height, index);
    CPubKey pubKey = txUid.get<CPubKey>();
    CKeyID keyId   = pubKey.IsFullyValid() ? txUid.get<CPubKey>().GetKeyId() : Hash160(regId.GetRegIdRaw());
    // Contstuct an empty account log which will delete account automatically if the blockchain rollbacked.

    account.nickid = CNickID();
    account.owner_pubkey = pubKey;
    account.regid  = regId;
    account.keyid  = keyId;

    switch (coinType) {
        case CoinType::WICC: account.OperateBalance("WICC", ADD_FREE, coins); break;
        case CoinType::WUSD: account.OperateBalance("WUSD", ADD_FREE, coins); break;
        case CoinType::WGRT: account.OperateBalance("WGRT", ADD_FREE, coins); break;
        default: return ERRORMSG("CCoinRewardTx::ExecuteTx, invalid coin type");
    }

    if (!cw.accountCache.SaveAccount(account))
        return state.DoS(100, ERRORMSG("CCoinRewardTx::ExecuteTx, write secure account info error"),
            UPDATE_ACCOUNT_FAIL, "bad-save-accountdb");

    if (!SaveTxAddresses(height, index, cw, state, {txUid}))
        return false;

    return true;
}

string CCoinRewardTx::ToString(CAccountDBCache &accountCache) {
    return strprintf("txType=%s, hash=%s, ver=%d, account=%s, addr=%s, coinType=%d, coins=%ld\n", GetTxType(nTxType),
                     GetHash().ToString(), nVersion, txUid.ToString(),
                     txUid.get<CPubKey>().IsFullyValid() ? txUid.get<CPubKey>().GetKeyId().ToAddress() : "", coinType,
                     coins);
}

Object CCoinRewardTx::ToJson(const CAccountDBCache &accountCache) const {
    Object result;


    result.push_back(Pair("txid", GetHash().GetHex()));
    result.push_back(Pair("tx_type",        GetTxType(nTxType)));
    result.push_back(Pair("ver",            nVersion));
    result.push_back(Pair("uid",            txUid.ToString()));
    result.push_back(Pair("addr",           txUid.get<CPubKey>().IsFullyValid() ? txUid.get<CPubKey>().GetKeyId().ToAddress() : ""));
    result.push_back(Pair("coin_type",      GetCoinTypeName(CoinType(coinType))));
    result.push_back(Pair("coins",          coins));
    result.push_back(Pair("valid_height",   nValidHeight));

    return result;
}

bool CCoinRewardTx::GetInvolvedKeyIds(CCacheWrapper &cw, set<CKeyID> &keyIds) {
    keyIds.insert(txUid.get<CPubKey>().GetKeyId());

    return true;
}