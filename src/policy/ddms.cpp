#include <policy/ddms.h>

#include <cmath>
#include <numeric>

#include <chain.h>
#include <chainparams.h>
#include <logging.h>
#include <timedata.h>
#include <validation.h>
#include <consensus/tx_verify.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <util/strencodings.h>

MinerLicenses minerLicenses{};
MiningMechanism miningMechanism{};
const CScript WDMO_SCRIPT = CScript() << OP_HASH160 << ParseHex("1ce8c0d99a03210c680ca2c56ff71c332d39f237") << OP_EQUAL; // TODO: replace with actual wdmo script
const uint16_t MINING_ROUND_SIZE{ 100 };
const uint32_t FIRST_MINING_ROUND_HEIGHT{ 50000 }; // TODO: change to proper value
const uint32_t MAX_CLOSED_ROUND_TIME { MAX_FUTURE_BLOCK_TIME * 4 };

std::string MiningUtil::CScriptToString(const CScript& scriptPubKey) {
	auto scriptStr = HexStr(scriptPubKey.begin(), scriptPubKey.end());

	if (scriptPubKey.IsPayToScriptHash())
		return scriptStr.substr(4, scriptStr.size() - 6);
	else if (scriptPubKey.IsPayToPublicKeyHash())
		return scriptStr.substr(6, scriptStr.size() - 10);

	// pay-to-witness-script-hash
	return scriptStr.substr(4);
}

CBlockIndex* MiningUtil::FindBlockIndex(const uint32_t blockNumber) {
	auto blockIndex = chainActive.Tip();

	while (blockIndex && blockIndex->nHeight != blockNumber)
		blockIndex = blockIndex->pprev;

	return blockIndex;
}

uint32_t MiningUtil::FindRoundStartBlockNumber(const uint32_t blockNumber, const int heightThreshold) {
	int res = blockNumber - (blockNumber % MINING_ROUND_SIZE);
	return res < heightThreshold ? heightThreshold : res;
}

uint32_t MiningUtil::FindRoundEndBlockNumber(const uint32_t blockNumber, const uint32_t heightThreshold) {
	int startBlockNumber = FindRoundStartBlockNumber(blockNumber, heightThreshold);
	int offset = (MINING_ROUND_SIZE - startBlockNumber - 1) % MINING_ROUND_SIZE;
	int endBlockNumber = startBlockNumber + (offset < 0 ? MINING_ROUND_SIZE + offset : offset);

	return endBlockNumber >= chainActive.Tip()->nHeight ? chainActive.Tip()->nHeight : endBlockNumber;
}

void MinerLicenses::HandleTx(const CBaseTransaction& tx, const int height) {
	for (const auto& entry : ExtractLicenseEntries(tx, height)) {
		if (!FindLicense(entry)) {
			if (entry.hashRates.back().hashRate > 0)
				AddLicense(entry);
		} else
			ModifyLicense(entry);
	}
}

std::vector<MinerLicenses::LicenseEntry> MinerLicenses::ExtractLicenseEntries(const CBaseTransaction& tx, const int height) {
	std::vector<MinerLicenses::LicenseEntry> entries;

	for (const auto& vout : tx.vout)
		if (IsLicenseTxHeader(vout.scriptPubKey))
			entries.push_back(ExtractLicenseEntry(vout.scriptPubKey, height));

	return entries;
}

/**
 * License TX consists of:
 * OP_RETURN - 1 byte
 * data size - 1 byte
 * license header - 3 bytes by default
 * script - 20-32 bytes
 * hashrate in PH - 2 bytes
 */
MinerLicenses::LicenseEntry MinerLicenses::ExtractLicenseEntry(const CScript& scriptPubKey, const int height) {
	const int dataSize = scriptPubKey.size();
	const int SCRIPT_OFFSET = 5;
	const uint16_t hashRate = scriptPubKey[dataSize - 2] << 8 | scriptPubKey[dataSize - 1];
	const std::string script = HexStr(scriptPubKey.begin() + SCRIPT_OFFSET, scriptPubKey.begin() + SCRIPT_OFFSET + MinerScriptSize(scriptPubKey));

	return MinerLicenses::LicenseEntry{height, hashRate, script};
}

uint32_t MinerLicenses::MinerScriptSize(const CScript& scriptPubKey) const {
	const int OPCODE_SIZE = 1;
	const int DATALENGTH_SIZE = 1;
	const int HEADER_SIZE = 3;
	const int HASHRATE_SIZE = 2;
	return scriptPubKey.size() - OPCODE_SIZE - DATALENGTH_SIZE - HEADER_SIZE - HASHRATE_SIZE;
}

MinerLicenses::LicenseEntry* MinerLicenses::FindLicense(const std::string& script) const {
	auto it = std::find_if(std::begin(licenses), std::end(licenses), [&script](const MinerLicenses::LicenseEntry& license) {
		return license.script == script;
	});

	return it != std::end(licenses) ? const_cast<MinerLicenses::LicenseEntry*>(&*it) : nullptr;
}

MinerLicenses::LicenseEntry* MinerLicenses::FindLicense(const MinerLicenses::LicenseEntry& entry) const {
	return FindLicense(entry.script);
}

bool MinerLicenses::NeedToUpdateLicense(const MinerLicenses::LicenseEntry& entry) const {
	auto license = FindLicense(entry);
	return license != nullptr && license->hashRates.back().height < entry.hashRates.back().height;
}

void MinerLicenses::PushLicense(const int height, const uint16_t hashRate, const std::string& script) {
	auto it = std::find_if(std::begin(licenses), std::end(licenses), [=](const MinerLicenses::LicenseEntry& obj) {
		return obj.script == script;
	});

	if (it == std::end(licenses))
		licenses.emplace_back(height, hashRate, script);
	else {
		auto it2 = std::find_if(std::begin(it->hashRates), std::end(it->hashRates), [=](const MinerLicenses::HashrateInfo& obj) {
			return obj.height == height && obj.hashRate == hashRate;
		});

		if (it2 == std::end(it->hashRates))
			it->hashRates.emplace_back(height, hashRate);
	}
}

void MinerLicenses::AddLicense(const MinerLicenses::LicenseEntry& entry) {
	licenses.emplace_back(entry);
}

void MinerLicenses::ModifyLicense(const MinerLicenses::LicenseEntry& entry) {
	if (!NeedToUpdateLicense(entry))
		return;

	auto license = FindLicense(entry);

	for (const auto& hrInfo : entry.hashRates)
		if (hrInfo.height > license->hashRates.back().height)
			license->hashRates.emplace_back(hrInfo.height, hrInfo.hashRate);
}

bool MinerLicenses::AllowedMiner(const CScript& scriptPubKey) const {
	return FindLicense(MiningUtil::CScriptToString(scriptPubKey));
}

float MinerLicenses::GetHashrateSum(const int blockHeight, const int heightThreshold) const {
	return std::accumulate(std::begin(licenses), std::end(licenses), 0.0f, [=](float result, const MinerLicenses::LicenseEntry& license) {
		return result + GetMinerHashrate(license.script, blockHeight, heightThreshold);
	});
}

bool MiningMechanism::IsBlockLastOneInRound(const int blockHeight) const {
	return (blockHeight % MINING_ROUND_SIZE) == (MINING_ROUND_SIZE - 1);
}

int MiningMechanism::GetMinerBlockQuota(const CScript& scriptPubKey) {
	const auto scriptStrAddr = MiningUtil::CScriptToString(scriptPubKey);
	return minersBlockQuota[scriptStrAddr];
}

int MiningMechanism::GetMinerBlockLeftInRound(const CScript& scriptPubKey) {
	const auto scriptStrAddr = MiningUtil::CScriptToString(scriptPubKey);
	return minersBlockLeftInRound[scriptStrAddr];
}

void MiningMechanism::DecrementMinerBlockLimitInRound(const CScript& scriptPubKey) {
	const auto scriptStrAddr = MiningUtil::CScriptToString(scriptPubKey);
    --minersBlockLeftInRound[scriptStrAddr];
}

std::unordered_map<std::string, int> MiningMechanism::CalcMinersBlockQuota(const int blockHeight, const int heightThreshold) {
	minersBlockQuota.clear();
	auto licenses = minerLicenses.GetLicenses();
	const float hashrateSum = minerLicenses.GetHashrateSum(blockHeight, heightThreshold);

	for (const auto& license : licenses)
		minersBlockQuota[license.script] = std::max(1.0f, std::round(MINING_ROUND_SIZE * minerLicenses.GetMinerHashrate(license.script, blockHeight, heightThreshold) / hashrateSum));

	return minersBlockQuota;
}

std::unordered_map<std::string, int> MiningMechanism::CalcMinersBlockLeftInRound(const int blockHeight, const int heightThreshold) {
	if (minersBlockQuota.empty())
		minersBlockQuota = CalcMinersBlockQuota(blockHeight, heightThreshold);

	minersBlockLeftInRound = {minersBlockQuota};
	auto licenses = minerLicenses.GetLicenses();

	auto blockIndex = chainActive.Tip();
	const int endBlockNumber = MiningUtil::FindRoundEndBlockNumber(blockIndex->nHeight, heightThreshold);

	if (IsBlockLastOneInRound(endBlockNumber))
		return minersBlockLeftInRound;

	auto currentBlockIndex = MiningUtil::FindBlockIndex(endBlockNumber);
	const int startBlockNumber = MiningUtil::FindRoundStartBlockNumber(blockIndex->nHeight, heightThreshold);

	while (currentBlockIndex && currentBlockIndex->nHeight >= startBlockNumber) {
		CBlock block;
		ReadBlockFromDisk(block, currentBlockIndex, Params().GetConsensus());

		for (const auto& out : block.vtx[0]->vout) {
			const auto scriptStrAddr = MiningUtil::CScriptToString(out.scriptPubKey);
			if (minersBlockLeftInRound.find(scriptStrAddr) != std::end(minersBlockLeftInRound))
				--minersBlockLeftInRound[scriptStrAddr];
		}

		currentBlockIndex = currentBlockIndex->pprev;
	}

	return minersBlockLeftInRound;
}

bool MiningMechanism::CanMine(const CScript& scriptPubKey, const CBlock& newBlock, const int heightThreshold) {
	return (!IsClosedRingRound(scriptPubKey, newBlock, heightThreshold) || GetMinerBlockLeftInRound(scriptPubKey) > 0)
			&& minerLicenses.GetMinerHashrate(MiningUtil::CScriptToString(scriptPubKey), chainActive.Tip()->nHeight, heightThreshold);
}

bool MiningMechanism::IsClosedRingRound(const CScript& scriptPubKey, const CBlock& newBlock, const int heightThreshold) {
	if (CalcSaturatedMinersPower(chainActive.Tip()->nHeight, heightThreshold) >= 0.5f)
		return false;

	if (newBlock.nTime > chainActive.Tip()->nTime + GetTimeOffset() + MAX_CLOSED_ROUND_TIME
	|| IsOpenRingRoundTimestampConditionFulfilled(heightThreshold)) {
		return false;
	}

	return true;
}

bool MiningMechanism::IsOpenRingRoundTimestampConditionFulfilled(const int heightThreshold) {
	auto blockIndex = chainActive.Tip();
	auto prevBlockIndex = blockIndex->pprev;
	const int startBlockNumber = MiningUtil::FindRoundStartBlockNumber(blockIndex->nHeight, heightThreshold);

	while (prevBlockIndex->nHeight >= startBlockNumber) {
		if (blockIndex->nTime > prevBlockIndex->nTime + GetTimeOffset() + MAX_CLOSED_ROUND_TIME)
			return true;

		blockIndex = prevBlockIndex;
		prevBlockIndex = prevBlockIndex->pprev;
	}

	return false;
}

float MinerLicenses::GetMinerHashrate(const std::string& script, const int blockHeight, const int heightThreshold) const {
	auto license = FindLicense(script);
	if (!license)
		return 0;

	if (miningMechanism.IsBlockLastOneInRound(blockHeight))
		return license->hashRates.back().hashRate;

	const int startBlockNumber = MiningUtil::FindRoundStartBlockNumber(blockHeight, heightThreshold);

	for (auto it = license->hashRates.rbegin(); it !=license->hashRates.rend(); ++it)
		if (it->height < startBlockNumber)
			return it->hashRate;

	return 0;
}

float MiningMechanism::CalcSaturatedMinersPower(const int blockHeight, const uint32_t heightThreshold) {
	if (minersBlockLeftInRound.empty())
		minersBlockLeftInRound = CalcMinersBlockLeftInRound(blockHeight, heightThreshold);

	float saturatedPower = 0.0f;
	const float hashrateSum = minerLicenses.GetHashrateSum(blockHeight, heightThreshold);

	for (const auto& entry : minersBlockLeftInRound)
		if (entry.second <= 0)
			saturatedPower += minerLicenses.GetMinerHashrate(entry.first, blockHeight, heightThreshold);

	return saturatedPower / hashrateSum;
}

void MinerLicenses::RemoveLicense(MinerLicenses::LicenseEntry& entry) {
	auto it = std::find_if(std::begin(licenses), std::end(licenses), [&entry](const LicenseEntry& license) {
		return entry.script == license.script;
	});

	if (it != std::end(licenses))
		licenses.erase(it);
}

void MiningMechanism::EraseInvalidLicenses(const int blockHeight, const int heightThreshold) {
	std::vector<MinerLicenses::LicenseEntry> toErase;

	for (const auto& license : minerLicenses.GetLicenses()) {
		if (IsBlockLastOneInRound(blockHeight)) {
			if (license.hashRates.back().hashRate == 0)
				toErase.push_back(license);
		}
	}

	for (auto& license : toErase)
		minerLicenses.RemoveLicense(license);
}

void MiningMechanism::RecalculateBlockLimits(const int blockHeight, const int heightThreshold) {
	CalcMinersBlockQuota(blockHeight, heightThreshold);
	CalcMinersBlockLeftInRound(blockHeight, heightThreshold);
}
