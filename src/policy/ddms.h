#include <stdint.h>
#include <string>
#include <vector>

class CBaseTransaction;
class CScript;

class MinerLicenses {
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

public:
	void HandleTx(const CBaseTransaction& tx, const int height);
	const std::vector<LicenseEntry>& GetLicenses() { return licenses; }
	void PushLicense(const int height, const uint16_t hashRate, const std::string& address);
	bool AllowedMiner(const CScript& scriptPubKey) const;

private:
	void AddLicense(const LicenseEntry& license);
	void ModifyLicense(const LicenseEntry& license);
	LicenseEntry* FindLicense(const LicenseEntry& entry) const;
	LicenseEntry* FindLicense(const std::string& address) const;
	std::vector<LicenseEntry> ExtractLicenseEntries(const CBaseTransaction& tx, const int height);
	LicenseEntry ExtractLicenseEntry(const CScript& scriptPubKey, const int height);
	bool NeedToUpdateLicense(const LicenseEntry& entry) const;
	uint32_t MinerScriptSize(const CScript& scriptPubKey) const;

	std::vector<LicenseEntry> licenses;
};

extern MinerLicenses minerLicenses;
extern const CScript WDMO_SCRIPT;
