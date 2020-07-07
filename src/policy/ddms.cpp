#include <policy/ddms.h>

#include <cmath>
#include <numeric>

#include <chain.h>
#include <chainparams.h>
#include <validation.h>
#include <timedata.h>
#include <consensus/tx_verify.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <util/strencodings.h>

MinerLicenses minerLicenses{};
MiningMechanism miningMechanism{};
const CScript WDMO_SCRIPT = CScript() << OP_HASH160 << std::vector<unsigned char>{11, 182, 127, 3, 232, 176, 211, 69, 45, 165, 222, 55, 211, 47, 198, 174, 240, 165, 160, 160} << OP_EQUAL; // TODO: replace with actual wdmo script; use ParseHex
const uint16_t MINING_ROUND_SIZE{ 100 };
const uint32_t FIRST_MINING_ROUND_HEIGHT{ 35000 }; // TODO: change to proper value
const uint32_t MAX_CLOSED_ROUND_TIME { MAX_FUTURE_BLOCK_TIME * 5 };

void MinerLicenses::HandleTx(const CBaseTransaction& tx, const int height) {
	for (const auto& entry : ExtractLicenseEntries(tx, height)) {
		if (!FindLicense(entry))
			AddLicense(entry);
		else
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
	const int size = scriptPubKey.size();
	uint16_t hashRate = scriptPubKey[size - 2] << 8 | scriptPubKey[size - 1];
	std::string address = HexStr(scriptPubKey.begin() + 5, scriptPubKey.begin() + 5 + MinerScriptSize(scriptPubKey));

	return MinerLicenses::LicenseEntry{height, hashRate, address};
}

uint32_t MinerLicenses::MinerScriptSize(const CScript& scriptPubKey) const {
	const int OPCODE_SIZE = 1;
	const int DATALENGTH_SIZE = 1;
	const int HEADER_SIZE = 3;
	const int HASHRATE_SIZE = 2;
	return scriptPubKey.size() - OPCODE_SIZE - DATALENGTH_SIZE - HEADER_SIZE - HASHRATE_SIZE;
}

MinerLicenses::LicenseEntry* MinerLicenses::FindLicense(const std::string& address) const {
	auto it = std::find_if(std::begin(licenses), std::end(licenses), [&address](const MinerLicenses::LicenseEntry& license) {
		return license.address == address;
	});

	return it != std::end(licenses) ? const_cast<MinerLicenses::LicenseEntry*>(&*it) : nullptr;
}

MinerLicenses::LicenseEntry* MinerLicenses::FindLicense(const MinerLicenses::LicenseEntry& entry) const {
	return FindLicense(entry.address);
}

bool MinerLicenses::NeedToUpdateLicense(const MinerLicenses::LicenseEntry& entry) const {
	auto license = FindLicense(entry);
	return license != nullptr && license->height < entry.height;
}

void MinerLicenses::PushLicense(const int height, const uint16_t hashRate, const std::string& address) {
	auto it = std::find_if(std::begin(licenses), std::end(licenses), [&address](const MinerLicenses::LicenseEntry& obj) {
		return obj.address == address;
	});

	if (it == std::end(licenses))
		licenses.emplace_back(height, hashRate, address);
}

void MinerLicenses::AddLicense(const MinerLicenses::LicenseEntry& entry) {
	if (FindLicense(entry))
		return;

	licenses.emplace_back(entry.height, entry.hashRate, entry.address);
}

void MinerLicenses::ModifyLicense(const MinerLicenses::LicenseEntry& entry) {
	auto license = FindLicense(entry);
	if (!license || !NeedToUpdateLicense(entry))
		return;

	if (entry.hashRate == 0)
		licenses.erase(std::find(std::begin(licenses), std::end(licenses), *license));
	else {
		license->hashRate = entry.hashRate;
		license->height = entry.height;
	}
}

std::string CScriptToAddressString(const CScript& scriptPubKey) {
	auto scriptStr = HexStr(scriptPubKey.begin(), scriptPubKey.end());
	return scriptStr.substr(4, scriptStr.size() - 6);
}

bool MinerLicenses::AllowedMiner(const CScript& scriptPubKey) const {
	return FindLicense(CScriptToAddressString(scriptPubKey));
}

float MinerLicenses::GetHashrateSum() const {
	return std::accumulate(std::begin(licenses), std::end(licenses), 0.0f, [](float result, const MinerLicenses::LicenseEntry& license) {
		return result + license.hashRate;
	});
}

std::unordered_map<std::string, float> MiningMechanism::CalcMinersBlockAverageOnAllRounds(uint32_t heightThreshold) {
	std::unordered_map<std::string, float> minersBlockAverage;
	auto blockIndex = chainActive.Tip();
	uint16_t rounds = blockIndex->nHeight % MINING_ROUND_SIZE != MINING_ROUND_SIZE - 1;

	while (blockIndex->nHeight >= heightThreshold) {
		CBlock block;
		ReadBlockFromDisk(block, blockIndex, Params().GetConsensus());

		for (const auto& out : block.vtx[0]->vout) {
			const auto scriptStrAddr = CScriptToAddressString(out.scriptPubKey);
			if (!minerLicenses.AllowedMiner(out.scriptPubKey))
				continue;

			if (minersBlockAverage.find(scriptStrAddr) == std::end(minersBlockAverage))
				minersBlockAverage[scriptStrAddr] = 1;
			else
				++minersBlockAverage[scriptStrAddr];
		}

		if (blockIndex->nHeight % MINING_ROUND_SIZE == MINING_ROUND_SIZE - 1)
			++rounds;

		blockIndex = blockIndex->pprev;
	}

	for (auto& entry : minersBlockAverage)
		entry.second /= rounds;

	return minersBlockAverage;
}

float MiningMechanism::CalcMinerBlockAverageOnAllRounds(const CScript& scriptPubKey, const uint32_t heightThreshold) {
	const auto scriptStrAddr = CScriptToAddressString(scriptPubKey);
	return CalcMinersBlockAverageOnAllRounds(heightThreshold)[scriptStrAddr];
}

std::unordered_map<std::string, int> MiningMechanism::CalcMinersBlockQuota() {
	std::unordered_map<std::string, int> minersBlockQuota;
	auto licenses = minerLicenses.GetLicenses();
	float hashrateSum = minerLicenses.GetHashrateSum();

	for (const auto& license : licenses)
		minersBlockQuota[license.address] = std::round(MINING_ROUND_SIZE * license.hashRate / hashrateSum);

	return minersBlockQuota;
}

uint16_t MiningMechanism::CalcMinerBlockQuota(const CScript& scriptPubKey) {
	const auto scriptStrAddr = CScriptToAddressString(scriptPubKey);
	return CalcMinersBlockQuota()[scriptStrAddr];
}

std::unordered_map<std::string, int> MiningMechanism::CalcMinersBlockLeftInRound(const uint32_t heightThreshold) {
	std::unordered_map<std::string, int> minersBlockQuota = CalcMinersBlockQuota();
	std::unordered_map<std::string, int> minersBlockLeftInRound(minersBlockQuota);
	auto licenses = minerLicenses.GetLicenses();

	auto blockIndex = chainActive.Tip();
	auto currentBlockIndex = FindBlockIndex(FindRoundEndBlockNumber(blockIndex->nHeight, blockIndex->nHeight));

	while (currentBlockIndex && currentBlockIndex->nHeight >= FindRoundStartBlockNumber(blockIndex->nHeight, heightThreshold)) {
		CBlock block;
		ReadBlockFromDisk(block, currentBlockIndex, Params().GetConsensus());

		for (const auto& out : block.vtx[0]->vout) {
			const auto scriptStrAddr = CScriptToAddressString(out.scriptPubKey);
			if (minersBlockLeftInRound.find(scriptStrAddr) != std::end(minersBlockLeftInRound))
				--minersBlockLeftInRound[scriptStrAddr];
		}

		currentBlockIndex = currentBlockIndex->pprev;
	}

	return minersBlockLeftInRound;
}

uint16_t MiningMechanism::CalcMinerBlockLeftInRound(const CScript& scriptPubKey, const uint32_t heightThreshold) {
	const auto scriptStrAddr = CScriptToAddressString(scriptPubKey);
	return CalcMinersBlockLeftInRound(heightThreshold)[scriptStrAddr];
}

CBlockIndex* MiningMechanism::FindBlockIndex(const uint32_t blockNumber) {
	auto blockIndex = chainActive.Tip();

	while (blockIndex && blockIndex->nHeight != blockNumber)
		blockIndex = blockIndex->pprev;

	return blockIndex;
}

uint32_t MiningMechanism::FindRoundStartBlockNumber(const uint32_t blockNumber, const int heightThreshold) {
	auto res = blockNumber - (blockNumber % MINING_ROUND_SIZE);
	return res < heightThreshold ? heightThreshold : res;
}

uint32_t MiningMechanism::FindRoundEndBlockNumber(const uint32_t blockNumber, const uint32_t tipBlockNumber, const uint32_t heightThreshold) {
	if (blockNumber >= tipBlockNumber
	|| FindRoundStartBlockNumber(blockNumber, heightThreshold) == FindRoundStartBlockNumber(tipBlockNumber, heightThreshold))
		return tipBlockNumber;

	return FindRoundStartBlockNumber(blockNumber, heightThreshold) + MINING_ROUND_SIZE - 1;
}

bool MiningMechanism::CanMine(const CScript& scriptPubKey, const CBlock& newBlock, const uint32_t heightThreshold) {
	return !IsClosedRingRound(scriptPubKey, newBlock, heightThreshold) || CalcMinerBlockLeftInRound(scriptPubKey, heightThreshold) > 0;;
}

bool MiningMechanism::IsClosedRingRound(const CScript& scriptPubKey, const CBlock& newBlock, const uint32_t heightThreshold) {
	if (CalcSaturatedMinersPower(heightThreshold) >= 0.5f)
		return false;

	auto blockIndex = chainActive.Tip();
	if (newBlock.nTime > blockIndex->nTime + GetTimeOffset() + MAX_CLOSED_ROUND_TIME
	|| IsOpenRingRoundTimestampConditionFulfilled(heightThreshold)) {
		return false;
	}

	return true;
}

bool MiningMechanism::IsOpenRingRoundTimestampConditionFulfilled(const uint32_t heightThreshold) {
	auto blockIndex = chainActive.Tip();
	auto prevBlockIndex = blockIndex->pprev;
	auto startBlockNumber = FindRoundStartBlockNumber(blockIndex->nHeight, heightThreshold);

	while (prevBlockIndex->nHeight >= startBlockNumber) {
		if (blockIndex->nTime > prevBlockIndex->nTime + GetTimeOffset() + MAX_CLOSED_ROUND_TIME)
			return true;

		blockIndex = prevBlockIndex;
		prevBlockIndex = prevBlockIndex->pprev;
	}

	return false;
}

float MinerLicenses::GetMinerHashrate(const std::string& script) {
	auto license = FindLicense(script);
	return license ? license->hashRate : 0;
}

float MiningMechanism::CalcSaturatedMinersPower(const uint32_t heightThreshold) {
	auto minersBlockLeftInRound = CalcMinersBlockLeftInRound(heightThreshold);
	float saturatedPower = 0.0f;
	uint32_t hashrateSum = minerLicenses.GetHashrateSum();

	for (const auto& entry : minersBlockLeftInRound)
		if (entry.second <= 0)
			saturatedPower += (float)minerLicenses.FindLicense(entry.first)->hashRate / hashrateSum;

	return saturatedPower;
}
