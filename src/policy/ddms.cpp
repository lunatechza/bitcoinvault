#include <policy/ddms.h>

#include <consensus/tx_verify.h>
#include <primitives/transaction.h>
#include <util/strencodings.h>

const CScript WDMO_SCRIPT = CScript() << OP_HASH160 << std::vector<unsigned char>{11, 182, 127, 3, 232, 176, 211, 69, 45, 165, 222, 55, 211, 47, 198, 174, 240, 165, 160, 160} << OP_EQUAL; // TODO: replace with actual wdmo script; use ParseHex
MinerLicenses minerLicenses{};

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

bool MinerLicenses::AllowedMiner(const CScript& scriptPubKey) const {
	auto scriptStr = HexStr(scriptPubKey.begin(), scriptPubKey.end());
	scriptStr = scriptStr.substr(4, scriptStr.size() - 6);

	return FindLicense(scriptStr);
}
