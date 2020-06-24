#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <index/txindex.h>
#include <policy/ddms.h>
#include <script/script.h>
#include <test/test_bitcoin.h>
#include <util/memory.h>

#include <memory>

#include <boost/test/unit_test.hpp>

static CMutableTransaction createCoinbase() {
	CMutableTransaction coinbaseTx;

	coinbaseTx.nVersion = 1;
	coinbaseTx.vin.resize(1);
	coinbaseTx.vout.resize(1);
	coinbaseTx.vin[0].scriptSig = CScript() << OP_11 << OP_EQUAL;
	coinbaseTx.vin[0].prevout.SetNull();
	coinbaseTx.vout[0].nValue = 50000;
	coinbaseTx.vout[0].scriptPubKey = WDMO_SCRIPT;

	return coinbaseTx;
}

static CScript createLicensedMinerScript() {
	std::vector<unsigned char> data;
	data.insert(std::end(data), {0xA9, 0x14, 0xE6, 0x35, 0xF7, 0x6A, 0x0F, 0xBD, 0xB1, 0x70, 0x56, 0x58, 0xA1, 0xE3, 0xB0, 0x56, 0x73, 0x78, 0x07, 0x4A});

	CScript minerScript;

	minerScript << OP_HASH160;
	minerScript << data;
	minerScript << OP_EQUAL;

	return minerScript;
}

static CScript createLicenseScript() {
	std::vector<unsigned char> data;
	data.insert(std::end(data), {0x4C, 0x54, 0x78}); // license header
	data.insert(std::end(data), {0xA9, 0x14, 0xE6, 0x35, 0xF7, 0x6A, 0x0F, 0xBD, 0xB1, 0x70, 0x56, 0x58, 0xA1, 0xE3, 0xB0, 0x56, 0x73, 0x78, 0x07, 0x4A}); // miner license script
	data.insert(std::end(data), {0x00, 0x05}); // hashrate

	CScript licenseScript;

	licenseScript << OP_RETURN;
	licenseScript << data;

	return licenseScript;
}

static CMutableTransaction createLicenseTransaction(const uint256 parentHash) {
	CMutableTransaction tx;
	tx.vin.resize(1);
	tx.vin[0].scriptSig = CScript() << OP_11;
	tx.vin[0].prevout.hash = parentHash;
	tx.vin[0].prevout.n = 0;
	tx.vout.resize(2);
	tx.vout[0].scriptPubKey = CScript();
	tx.vout[0].nValue = 49000;
	tx.vout[1].scriptPubKey = createLicenseScript();
	tx.vout[1].nValue = 0;

	return tx;
}

struct DdmsSetup : public TestChain100Setup {
	DdmsSetup()
		: TestChain100Setup() {
		minerLicenses = MinerLicenses{};
	}
	~DdmsSetup() = default;
};

BOOST_FIXTURE_TEST_SUITE(ddms_tests, DdmsSetup)

BOOST_AUTO_TEST_CASE(shouldIsLicenseTxHeaderReturnTrueWhenProcessingLTxScriptPubKey)
{
	CScript ltxScriptPubKey = createLicenseScript();
	BOOST_CHECK(IsLicenseTxHeader(ltxScriptPubKey));
}

BOOST_AUTO_TEST_CASE(shouldIsLicenseTxHeaderReturnFalseWhenNotProcessingLTxScriptPubKey)
{
	CScript fakeLtxScriptPubKey = createLicenseScript();
	--fakeLtxScriptPubKey[2]; // changing first byte of license header
	BOOST_CHECK(!IsLicenseTxHeader(fakeLtxScriptPubKey));
}

BOOST_AUTO_TEST_CASE(shouldIsLicenseTxReturnFalseWhenTxNullOrCoinbase)
{
	CMutableTransaction coinbaseTx, nullTx;
	coinbaseTx = createCoinbase();

	BOOST_CHECK(!IsLicenseTx(CTransaction(nullTx)));
	BOOST_CHECK(!IsLicenseTx(CTransaction(coinbaseTx)));
}

BOOST_AUTO_TEST_CASE(shouldIsLicenseTxReturnFalseWhenTxWasNotSentByWDMO)
{
	g_txindex = MakeUnique<TxIndex>(1 << 20, true);
	g_txindex->Start();

	CScript fakeWdmoScript = WDMO_SCRIPT; --fakeWdmoScript[2]; // change the first byte of script hash
	auto blk = CreateAndProcessBlock({}, fakeWdmoScript);
	CMutableTransaction lTx = createLicenseTransaction(blk.vtx[0]->GetHash());

	CreateAndProcessBlock({lTx}, WDMO_SCRIPT);

	// Allow tx index to catch up with the block index.
	constexpr int64_t timeout_ms = 10 * 1000;
	int64_t time_start = GetTimeMillis();
	while (!g_txindex->BlockUntilSyncedToCurrentChain()) {
		BOOST_REQUIRE(time_start + timeout_ms > GetTimeMillis());
		MilliSleep(100);
	}

	BOOST_CHECK(g_txindex->BlockUntilSyncedToCurrentChain());
	BOOST_CHECK(!IsLicenseTx(CTransaction(lTx)));

	g_txindex->Stop();
	g_txindex.reset();
}

BOOST_AUTO_TEST_CASE(shouldIsLicenseTxReturnFalseWhenSentByWDMOButNoLTxHeaderFound)
{
	auto blk = CreateAndProcessBlock({}, WDMO_SCRIPT);
	CMutableTransaction lTx = createLicenseTransaction(blk.vtx[0]->GetHash());
	lTx.vout[0].scriptPubKey = WDMO_SCRIPT;
	lTx.vout[1].scriptPubKey = CScript();

	CreateAndProcessBlock({lTx}, WDMO_SCRIPT);

	BOOST_CHECK(!IsLicenseTx(CTransaction(lTx)));
}

BOOST_AUTO_TEST_CASE(shouldIsLicenseTxReturnTrueIfLTxHeaderFoundAndSentByWDMOCheckedInTxIndex)
{
	g_txindex = MakeUnique<TxIndex>(1 << 20, true);
	g_txindex->Start();

	auto blk = CreateAndProcessBlock({}, WDMO_SCRIPT);
	CMutableTransaction lTx = createLicenseTransaction(blk.vtx[0]->GetHash());
	lTx.vout[0].scriptPubKey = WDMO_SCRIPT;
	CreateAndProcessBlock({lTx}, WDMO_SCRIPT);

	// Allow tx index to catch up with the block index.
	constexpr int64_t timeout_ms = 10 * 1000;
	int64_t time_start = GetTimeMillis();
	while (!g_txindex->BlockUntilSyncedToCurrentChain()) {
		BOOST_REQUIRE(time_start + timeout_ms > GetTimeMillis());
		MilliSleep(100);
	}

    BOOST_CHECK(g_txindex->BlockUntilSyncedToCurrentChain());
	BOOST_CHECK(IsLicenseTx(CTransaction(lTx)));

	g_txindex->Stop();
	g_txindex.reset();
}

BOOST_AUTO_TEST_CASE(shouldIsLicenseTxReturnTrueIfLTxHeaderFoundAndSentByWDMOCheckedInCoinsCacheView)
{
	auto blk = CreateAndProcessBlock({}, WDMO_SCRIPT);
	CMutableTransaction lTx = createLicenseTransaction(blk.vtx[0]->GetHash());
	lTx.vout[0].scriptPubKey = WDMO_SCRIPT;
	CreateAndProcessBlock({lTx}, WDMO_SCRIPT);

	BOOST_CHECK(IsLicenseTx(CTransaction(lTx)));
}

BOOST_AUTO_TEST_CASE(shouldAddLicenseIfCorrectLtxProvided)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 1);
	BOOST_CHECK_EQUAL(1, minerLicenses.GetLicenses().size());

	lTx.vout[1].scriptPubKey[5]++; // other miner's address
	minerLicenses.HandleTx(CTransaction(lTx), 2);
	BOOST_CHECK_EQUAL(2, minerLicenses.GetLicenses().size());
}

BOOST_AUTO_TEST_CASE(shouldNotAddLicenseIfAlreadyExists)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 1);
	minerLicenses.HandleTx(CTransaction(lTx), 2);
	BOOST_CHECK_EQUAL(1, minerLicenses.GetLicenses().size());
}

BOOST_AUTO_TEST_CASE(shouldOnlyModifyLicenseIfAlreadyPushed)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	std::string address{"a914e635f76a0fbdb1705658a1e3b0567378074a"};

	minerLicenses.PushLicense(1, 3, address);
	minerLicenses.HandleTx(CTransaction(lTx), 2);
	auto licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(1, licenses.size());
	BOOST_CHECK_EQUAL(5, licenses[0].hashRate);
}

BOOST_AUTO_TEST_CASE(shouldModifyLicenseIfAlreadyExists)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 1);
	auto licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(5, licenses[0].hashRate);

	lTx.vout[1].scriptPubKey[26] = 3; // modyfing hashrate
	minerLicenses.HandleTx(CTransaction(lTx), 2);
	licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(3, licenses[0].hashRate);
}

BOOST_AUTO_TEST_CASE(shouldRemoveLicenseIfNoHashrateAssigned)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 1);
	auto licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(5, licenses[0].hashRate);

	lTx.vout[1].scriptPubKey[26] = 0; // modyfing hashrate
	minerLicenses.HandleTx(CTransaction(lTx), 2);
	licenses = minerLicenses.GetLicenses();
	BOOST_CHECK(licenses.empty());
}

BOOST_AUTO_TEST_CASE(shouldNotModifyLicenseIfProvidedOlderEntry)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 2);
	auto licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(5, licenses[0].hashRate);

	lTx.vout[1].scriptPubKey[26] = 3; // modyfing hashrate
	minerLicenses.HandleTx(CTransaction(lTx), 1);
	licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(5, licenses[0].hashRate);
}

BOOST_AUTO_TEST_CASE(shouldPushLicenseIfNotExists)
{
	minerLicenses.PushLicense(1, 5, "a914e635f76a0fbdb1705658a1e3b0567378074a");
	auto licenses = minerLicenses.GetLicenses();

	BOOST_CHECK_EQUAL(1, licenses.size());
	BOOST_CHECK_EQUAL(5, licenses[0].hashRate);
}

BOOST_AUTO_TEST_CASE(shouldNotPushLicenseIfAlreadyExists)
{
	minerLicenses.PushLicense(1, 5, "a914e635f76a0fbdb1705658a1e3b0567378074a");
	minerLicenses.PushLicense(2, 3, "a914e635f76a0fbdb1705658a1e3b0567378074a");
	auto licenses = minerLicenses.GetLicenses();

	BOOST_CHECK_EQUAL(1, licenses.size());
	BOOST_CHECK_EQUAL(5, licenses[0].hashRate);
}

BOOST_AUTO_TEST_CASE(shouldAllowMineToLicensedMiner)
{
	auto minerScript = createLicensedMinerScript();
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());
	minerLicenses.HandleTx(CTransaction(lTx), 1);

	BOOST_CHECK(minerLicenses.AllowedMiner(minerScript));
}

BOOST_AUTO_TEST_CASE(shouldNotAllowMineToNotLicensedMiner)
{
	auto minerScript = createLicensedMinerScript(); --minerScript[2]; // change first byte of miner's script
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());
	minerLicenses.HandleTx(CTransaction(lTx), 1);

	BOOST_CHECK(!minerLicenses.AllowedMiner(minerScript));
}

BOOST_AUTO_TEST_SUITE_END()
