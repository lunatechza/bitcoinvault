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

class MinerLicenses {
public:
	struct LicenseEntry {
		LicenseEntry(const LicenseEntry& license) = default;
		LicenseEntry(const int height, const uint16_t hashRate, const std::string& address)
			: height(height), hashRate(hashRate), address(address) {}

		int height;
		uint16_t hashRate;
		std::string address;

		bool operator==(const LicenseEntry& rhs) {
			return height == rhs.height && hashRate == rhs.hashRate && address == rhs.address;
		}
	};

	void HandleTx(const CBaseTransaction& tx, const int height);
	const std::vector<LicenseEntry>& GetLicenses() { return licenses; }
	void PushLicense(const int height, const uint16_t hashRate, const std::string& address);
	bool AllowedMiner(const CScript& scriptPubKey) const;
	float GetHashrateSum() const;
	float GetMinerHashrate(const std::string& script);
	LicenseEntry* FindLicense(const LicenseEntry& entry) const;
	LicenseEntry* FindLicense(const std::string& address) const;

private:
	void AddLicense(const LicenseEntry& license);
	void ModifyLicense(const LicenseEntry& license);
	std::vector<LicenseEntry> ExtractLicenseEntries(const CBaseTransaction& tx, const int height);
	LicenseEntry ExtractLicenseEntry(const CScript& scriptPubKey, const int height);
	bool NeedToUpdateLicense(const LicenseEntry& entry) const;
	uint32_t MinerScriptSize(const CScript& scriptPubKey) const;

	std::vector<LicenseEntry> licenses;
};

class MiningMechanism {
public:
	std::unordered_map<std::string, int> CalcMinersBlockQuota();
	uint16_t CalcMinerBlockQuota(const CScript& scriptPubKey);
	std::unordered_map<std::string, int> CalcMinersBlockLeftInRound(const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	uint16_t CalcMinerBlockLeftInRound(const CScript& scriptPubKey, const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	std::unordered_map<std::string, float> CalcMinersBlockAverageOnAllRounds(uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	float CalcMinerBlockAverageOnAllRounds(const CScript& scriptPubKey, const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);

	bool CanMine(const CScript& scriptPubKey, const CBlock& newBlock, const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);
private:
	uint32_t FindRoundEndBlockNumber(const uint32_t blockNumber, const uint32_t tipBlockNumber, const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	uint32_t FindRoundStartBlockNumber(const uint32_t blockNumber, const int heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	CBlockIndex* FindBlockIndex(const uint32_t blockNumber);

	float CalcSaturatedMinersPower(const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);

	bool IsClosedRingRound(const CScript& scriptPubKey, const CBlock& newBlock, const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);
	bool IsOpenRingRoundTimestampConditionFulfilled(const uint32_t heightThreshold = FIRST_MINING_ROUND_HEIGHT);
};
