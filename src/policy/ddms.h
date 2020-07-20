#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>

class CBaseTransaction;
class CBlock;
class CBlockIndex;
class CScript;

class MinerLicenses;
class MiningMechanism;

/** In-memory data structure for current miner's licenses */
extern MinerLicenses minerLicenses;

/** Object to restrict ddms consensus rules for licensed miners */
extern MiningMechanism miningMechanism;

/** Script of the WDMO organization to ensure that miner's license modificaton comes from legit blockchain user */
extern const CScript WDMO_SCRIPT;

/** Mining round size in block number after which miner's limits will be reset */
extern const uint16_t MINING_ROUND_SIZE;

/** Block height at which first DDMS mining round will start */
extern const uint32_t FIRST_MINING_ROUND_HEIGHT;

/** Time needed to pass until last received block to let saturated miners mine again in current round (in seconds) */
extern const uint32_t MAX_CLOSED_ROUND_TIME;

class MiningUtil {
public:
	static uint32_t FindRoundEndBlockNumber(const uint32_t blockNumber, const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	static uint32_t FindRoundStartBlockNumber(const uint32_t blockNumber, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	static CBlockIndex* FindBlockIndex(const uint32_t blockNumber);
	static std::string CScriptToString(const CScript& scriptPubKey);
};

class MinerLicenses {
public:
	struct HashrateInfo {
		HashrateInfo(const HashrateInfo& hrInfo) = default;
		HashrateInfo(const int height, const uint16_t hashRate)
			: height(height), hashRate(hashRate) {};

		bool operator==(const HashrateInfo& rhs) const {
			return height == rhs.height && hashRate == rhs.hashRate;
		}

		int height;
		uint16_t hashRate;
	};
	struct LicenseEntry {
		LicenseEntry(const LicenseEntry& license) = default;
		LicenseEntry(const int height, const uint16_t hashRate, const std::string& address)
			: address(address) {
			hashRates.emplace_back(height, hashRate);
		}

		bool operator==(const LicenseEntry& rhs) const {
			return hashRates == rhs.hashRates && address == rhs.address;
		}

		std::vector<HashrateInfo> hashRates;
		std::string address;
	};

	void HandleTx(const CBaseTransaction& tx, const int height);
	const std::vector<LicenseEntry>& GetLicenses() { return licenses; }
	void PushLicense(const int height, const uint16_t hashRate, const std::string& address);
	void RemoveLicense(LicenseEntry& entry);
	bool AllowedMiner(const CScript& scriptPubKey) const;

	/* Gets sum of miners hashrate for current round */
	float GetHashrateSum(const int blockHeight, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT) const;
	/* Gets miner hashrate for current round; not necessaraly equal last hashrate entry */
	float GetMinerHashrate(const std::string& script, const int blockHeight, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT) const;

	LicenseEntry* FindLicense(const LicenseEntry& entry) const;
	LicenseEntry* FindLicense(const std::string& address) const;

	std::vector<LicenseEntry> ExtractLicenseEntries(const CBaseTransaction& tx, const int height);
private:
	void AddLicense(const LicenseEntry& license);
	void ModifyLicense(const LicenseEntry& license);
	LicenseEntry ExtractLicenseEntry(const CScript& scriptPubKey, const int height);
	bool NeedToUpdateLicense(const LicenseEntry& entry) const;
	uint32_t MinerScriptSize(const CScript& scriptPubKey) const;

	std::vector<LicenseEntry> licenses;
};

class MiningMechanism {
public:
	void RecalculateBlockLimits(const int blockHeight, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	void DecrementMinerBlockLimitInRound(const CScript& scriptPubKey);

	int GetMinerBlockQuota(const CScript& scriptPubKey);
	int GetMinerBlockLeftInRound(const CScript& scriptPubKey);

	bool CanMine(const CScript& scriptPubKey, const CBlock& newBlock, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	void EraseInvalidLicenses(const int blockHeight, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	bool IsBlockLastOneInRound(const int blockHeight) const;
private:
	std::unordered_map<std::string, int> CalcMinersBlockQuota(const int blockHeight, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	std::unordered_map<std::string, int> CalcMinersBlockLeftInRound(const int blockHeight, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT);

	float CalcSaturatedMinersPower(const int blockHeight, const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);

	bool IsClosedRingRound(const CScript& scriptPubKey, const CBlock& newBlock, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	bool IsOpenRingRoundTimestampConditionFulfilled(const int heightThreshold = FIRST_MINING_ROUND_HEIGHT);

	std::unordered_map<std::string, int> minersBlockQuota;
	std::unordered_map<std::string, int> minersBlockLeftInRound;
};
