#include <policy/ddms.h>

#include <cmath>
#include <numeric>

#include <chain.h>
#include <chainparams.h>
#include <validation.h>
#include <consensus/tx_verify.h>
#include <primitives/block.h>
#include <primitives/transaction.h>
#include <util/strencodings.h>

const CScript WDMO_SCRIPT = CScript() << OP_HASH160 << std::vector<unsigned char>{11, 182, 127, 3, 232, 176, 211, 69, 45, 165, 222, 55, 211, 47, 198, 174, 240, 165, 160, 160} << OP_EQUAL; // TODO: replace with actual wdmo script; use ParseHex
MinerLicenses minerLicenses{};
const uint16_t MINING_ROUND_SIZE{ 100 };
const uint32_t FIRST_MINING_ROUND_HEIGHT{ 35000 }; // TODO: change to proper value

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

std::unordered_map<std::string, float> MiningMechanism::CalcMinersBlockAverageOnAllRounds() {
	uint16_t rounds = 1;
	std::unordered_map<std::string, float> minersBlockAverage;
	auto blockIndex = chainActive.Tip();

	while (blockIndex->nHeight >= FIRST_MINING_ROUND_HEIGHT) {
		CBlock block;
		ReadBlockFromDisk(block, blockIndex, Params().GetConsensus());

		for (const auto& out : block.vtx[0]->vout) {
			const auto scriptStrAddr = CScriptToAddressString(out.scriptPubKey);
			if (minerLicenses.AllowedMiner(out.scriptPubKey))
				continue;

			if (minersBlockAverage.find(scriptStrAddr) == std::end(minersBlockAverage))
				minersBlockAverage[scriptStrAddr] = 1;
			else
				++minersBlockAverage[scriptStrAddr];
		}

		blockIndex = blockIndex->pprev;

		if( blockIndex->nHeight % MINING_ROUND_SIZE == MINING_ROUND_SIZE - 1)
			++rounds;
	}

	for (auto& entry : minersBlockAverage)
		entry.second /= rounds;

	return minersBlockAverage;
}

float MiningMechanism::CalcMinerBlockAverageOnAllRounds(const CScript& scriptPubKey) {
	const auto scriptStrAddr = CScriptToAddressString(scriptPubKey);
	return CalcMinersBlockAverageOnAllRounds()[scriptStrAddr];
}

std::unordered_map<std::string, uint16_t> MiningMechanism::CalcMinersBlockQuota() {
	std::unordered_map<std::string, uint16_t> minersBlockQuota;
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

std::unordered_map<std::string, uint16_t> MiningMechanism::CalcMinersBlockLeftInRound() {
	std::unordered_map<std::string, uint16_t> minersBlockLeftInRound;
	std::unordered_map<std::string, uint16_t> minersBlockQuota = CalcMinersBlockQuota();
	auto licenses = minerLicenses.GetLicenses();

	auto blockIndex = chainActive.Tip();
	auto currentBlockIndex = FindBlockIndex(FindRoundEndBlockNumber(blockIndex->nHeight, blockIndex->nHeight));

	while (currentBlockIndex->nHeight >= FindRoundStartBlockNumber(blockIndex->nHeight)) {
		CBlock block;
		ReadBlockFromDisk(block, blockIndex, Params().GetConsensus());

		for (const auto& out : block.vtx[0]->vout) {
			const auto scriptStrAddr = CScriptToAddressString(out.scriptPubKey);
			if (minersBlockQuota.find(scriptStrAddr) == std::end(minersBlockQuota))
				continue;

			if (minersBlockLeftInRound.find(scriptStrAddr) == std::end(minersBlockLeftInRound))
				minersBlockLeftInRound[scriptStrAddr] = minersBlockQuota[scriptStrAddr] - 1;
			else
				--minersBlockLeftInRound[scriptStrAddr];
		}

		currentBlockIndex = currentBlockIndex->pprev;
	}

	return minersBlockLeftInRound;
}

uint16_t MiningMechanism::CalcMinerBlockLeftInRound(const CScript& scriptPubKey) {
	const auto scriptStrAddr = CScriptToAddressString(scriptPubKey);
	return CalcMinersBlockLeftInRound()[scriptStrAddr];
}

CBlockIndex* MiningMechanism::FindBlockIndex(const uint32_t blockNumber) {
	auto blockIndex = chainActive.Tip();

	while (blockIndex->nHeight != blockNumber)
		blockIndex = blockIndex->pprev;

	return blockIndex;
}

uint32_t MiningMechanism::FindRoundStartBlockNumber(const uint32_t blockNumber) {
	return blockNumber - (blockNumber % MINING_ROUND_SIZE);
}

uint32_t MiningMechanism::FindRoundEndBlockNumber(const uint32_t blockNumber, const uint32_t tipBlockNumber) {
	if (blockNumber >= tipBlockNumber
	|| FindRoundStartBlockNumber(blockNumber) == FindRoundStartBlockNumber(tipBlockNumber))
		return tipBlockNumber;

	return FindRoundStartBlockNumber(blockNumber) + MINING_ROUND_SIZE - 1;
}
