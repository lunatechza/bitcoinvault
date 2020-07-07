#include <consensus/tx_verify.h>
#include <consensus/validation.h>
#include <index/txindex.h>
#include <policy/ddms.h>
#include <script/script.h>
#include <test/test_bitcoin.h>
#include <util/memory.h>
#include <validation.h>

#include <memory>

#include <boost/test/unit_test.hpp>

static const int TEST_HEIGHT_THRESHOLD = 1;

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
	data.insert(std::end(data), {0x60, 0x98, 0xD9, 0x46, 0xDF, 0x69, 0x5B, 0x6C, 0x87, 0x6B, 0x48, 0xC3, 0xE4, 0xC4, 0x15, 0x28, 0xED, 0x3A, 0x38, 0xDE});
	CScript minerScript;

	minerScript << OP_HASH160;
	minerScript << data;
	minerScript << OP_EQUAL;

	return minerScript;
}

static CScript createLicenseScript() {
	std::vector<unsigned char> data;
	data.insert(std::end(data), {0x4C, 0x54, 0x78}); // license header
	data.insert(std::end(data), {0x60, 0x98, 0xD9, 0x46, 0xDF, 0x69, 0x5B, 0x6C, 0x87, 0x6B, 0x48, 0xC3, 0xE4, 0xC4, 0x15, 0x28, 0xED, 0x3A, 0x38, 0xDE}); // miner license script
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
		: TestChain100Setup(false) {
		minerLicenses = MinerLicenses{};
		miningMechanism = MiningMechanism{};
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

	std::string address{"6098d946df695b6c876b48c3e4c41528ed3a38de"};

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
	minerLicenses.PushLicense(1, 5, "6098d946df695b6c876b48c3e4c41528ed3a38de");
	auto licenses = minerLicenses.GetLicenses();

	BOOST_CHECK_EQUAL(1, licenses.size());
	BOOST_CHECK_EQUAL(5, licenses[0].hashRate);
}

BOOST_AUTO_TEST_CASE(shouldNotPushLicenseIfAlreadyExists)
{
	minerLicenses.PushLicense(1, 5, "6098d946df695b6c876b48c3e4c41528ed3a38de");
	minerLicenses.PushLicense(2, 3, "6098d946df695b6c876b48c3e4c41528ed3a38de");
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

BOOST_AUTO_TEST_CASE(shouldCalculateHashrateSumOfMinersCorrectly)
{
	BOOST_CHECK_EQUAL(0.0f, minerLicenses.GetHashrateSum());

	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 1);

	lTx.vout[1].scriptPubKey[5]++; // other miner's address
	lTx.vout[1].scriptPubKey[25] = 1; // other miner's hashrate
	minerLicenses.HandleTx(CTransaction(lTx), 2);

	BOOST_CHECK_EQUAL(5 + ((1 << 8) + 5), minerLicenses.GetHashrateSum());
}

BOOST_AUTO_TEST_CASE(shouldReturnZeroHashrateIfMinerLicenseNotExists)
{
	std::string script{"6098d946df695b6c876b48c3e4c41528ed3a38de"};
	minerLicenses.PushLicense(1, 3, script);
	BOOST_CHECK_EQUAL(0, minerLicenses.GetMinerHashrate("ed83a3de82514c4e3c84b678c6b596fd649d8906"));
}

BOOST_AUTO_TEST_CASE(shouldReturnCorrectHashrateIfMinerLicenseExists)
{
	std::string script{"6098d946df695b6c876b48c3e4c41528ed3a38de"};
	minerLicenses.PushLicense(1, 3, script);
	BOOST_CHECK_EQUAL(3, minerLicenses.GetMinerHashrate("6098d946df695b6c876b48c3e4c41528ed3a38de"));
}

std::vector<std::string> prepareMinerLicenses() {
	std::string script{"6098d946df695b6c876b48c3e4c41528ed3a38de"};
	std::string script2{"6098d946df695b6c876b48c3e4c41528ed3a38dd"};
	std::string script3{"6098d946df695b6c876b48c3e4c41528ed3a38dc"};
	std::string script4{"6098d946df695b6c876b48c3e4c41528ed3a38db"};
	std::string script5{"6098d946df695b6c876b48c3e4c41528ed3a38da"};
	minerLicenses.PushLicense(1, 3, script);
	minerLicenses.PushLicense(1, 2, script2);
	minerLicenses.PushLicense(1, 1, script3);
	minerLicenses.PushLicense(1, 4, script4);
	minerLicenses.PushLicense(1, 5, script5);

	return std::vector<std::string>{script, script2, script3, script4, script5};
}

std::vector<CScript> prepareMinerScripts() {
	CScript scriptPubKey = createLicensedMinerScript();
	CScript scriptPubKey2 = createLicensedMinerScript(); scriptPubKey2[21] -= 1;
	CScript scriptPubKey3 = createLicensedMinerScript(); scriptPubKey3[21] -= 2;
	CScript scriptPubKey4 = createLicensedMinerScript(); scriptPubKey4[21] -= 3;
	CScript scriptPubKey5 = createLicensedMinerScript(); scriptPubKey5[21] -= 4;

	return std::vector<CScript>{scriptPubKey, scriptPubKey2, scriptPubKey3, scriptPubKey4, scriptPubKey5};
}

CBlock prepareValidBlock() {
	auto blockIndex = chainActive.Tip();
	CBlock block;
	block.nTime = blockIndex->nTime + 5 * MAX_CLOSED_ROUND_TIME;
	return block;
}

CBlock prepareInvalidBlock() {
	auto blockIndex = chainActive.Tip();
	CBlock block;
	block.nTime = blockIndex->nTime + 1;
	return block;
}

MinerLicenses::LicenseEntry prepareLicenseEntry() {
	return MinerLicenses::LicenseEntry{1, 1, "6098d946df695b6c876b48c3e4c41528ed3a38de"};
}

BOOST_AUTO_TEST_CASE(shouldFindLicenseReturnNullptrIfLicenseNotFound)
{
	BOOST_CHECK(!minerLicenses.FindLicense("6098d946df695b6c876b48c3e4c41528ed3a38de"));
	BOOST_CHECK(!minerLicenses.FindLicense(prepareLicenseEntry()));
}

BOOST_AUTO_TEST_CASE(shouldFindLicenseReturnLicenseIfExists)
{
	auto scripts = prepareMinerLicenses();
	BOOST_CHECK(minerLicenses.FindLicense(scripts[0]));
	BOOST_CHECK(minerLicenses.FindLicense(prepareLicenseEntry()));
}

BOOST_AUTO_TEST_CASE(shouldReturnCorrectMinersBlockQuotaBasedOnAssignedHashrate)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();
	auto minersBlockQuota = miningMechanism.CalcMinersBlockQuota();

	BOOST_CHECK_EQUAL(20, minersBlockQuota[scripts[0]]);
	BOOST_CHECK_EQUAL(13, minersBlockQuota[scripts[1]]);
	BOOST_CHECK_EQUAL(7, minersBlockQuota[scripts[2]]);
	BOOST_CHECK_EQUAL(27, minersBlockQuota[scripts[3]]);
	BOOST_CHECK_EQUAL(33, minersBlockQuota[scripts[4]]);

	BOOST_CHECK_EQUAL(20, miningMechanism.CalcMinerBlockQuota(pubkeys[0]));
	BOOST_CHECK_EQUAL(13, miningMechanism.CalcMinerBlockQuota(pubkeys[1]));
	BOOST_CHECK_EQUAL(7, miningMechanism.CalcMinerBlockQuota(pubkeys[2]));
	BOOST_CHECK_EQUAL(27, miningMechanism.CalcMinerBlockQuota(pubkeys[3]));
	BOOST_CHECK_EQUAL(33, miningMechanism.CalcMinerBlockQuota(pubkeys[4]));
}

BOOST_AUTO_TEST_CASE(shouldReturnEqualNumbersForBlocksLeftInRoundIfNoBlocksWereMined)
{
	prepareMinerLicenses();
	auto minersBlockQuota = miningMechanism.CalcMinersBlockQuota();
	auto minersBlockLeftInRound = miningMechanism.CalcMinersBlockLeftInRound(TEST_HEIGHT_THRESHOLD);

	BOOST_CHECK(minersBlockQuota == minersBlockLeftInRound);
}

std::string CScriptToAddressString(const CScript& scriptPubKey) {
	auto scriptStr = HexStr(scriptPubKey.begin(), scriptPubKey.end());
	return scriptStr.substr(4, scriptStr.size() - 6);
}

BOOST_AUTO_TEST_CASE(shouldReturnCorrectNumbersForBlocksLeftInRoundIfSomeBlocksWereMined)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	for (int i = 0; i < 3; ++i) {
		CreateAndProcessBlock({}, pubkeys[0]);
		CreateAndProcessBlock({}, pubkeys[1]);
	}
	CreateAndProcessBlock({}, pubkeys[2]);
	for (int i = 0; i < 2; ++i) {
		CreateAndProcessBlock({}, pubkeys[3]);
		CreateAndProcessBlock({}, pubkeys[4]);
	}

	auto minersBlockLeftInRound = miningMechanism.CalcMinersBlockLeftInRound(TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(17, minersBlockLeftInRound[scripts[0]]);
	BOOST_CHECK_EQUAL(10, minersBlockLeftInRound[scripts[1]]);
	BOOST_CHECK_EQUAL(6, minersBlockLeftInRound[scripts[2]]);
	BOOST_CHECK_EQUAL(25, minersBlockLeftInRound[scripts[3]]);
	BOOST_CHECK_EQUAL(31, minersBlockLeftInRound[scripts[4]]);

	BOOST_CHECK_EQUAL(17, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[0], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(10, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[1], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(6, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[2], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(25, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[3], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(31, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[4], TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldReturnCorrectNumbersForBlocksLeftInRoundIfAnyMinerSaturate)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	for (int i = 0; i < 3; ++i) {
		CreateAndProcessBlock({}, pubkeys[0]);
		CreateAndProcessBlock({}, pubkeys[1]);
	}
	CreateAndProcessBlock({}, pubkeys[2]);
	for (int i = 0; i < 2; ++i) {
		CreateAndProcessBlock({}, pubkeys[3]);
		CreateAndProcessBlock({}, pubkeys[4]);
	}

	auto minersBlockLeftInRound = miningMechanism.CalcMinersBlockLeftInRound(TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(17, minersBlockLeftInRound[scripts[0]]);
	BOOST_CHECK_EQUAL(10, minersBlockLeftInRound[scripts[1]]);
	BOOST_CHECK_EQUAL(6, minersBlockLeftInRound[scripts[2]]);
	BOOST_CHECK_EQUAL(25, minersBlockLeftInRound[scripts[3]]);
	BOOST_CHECK_EQUAL(31, minersBlockLeftInRound[scripts[4]]);

	BOOST_CHECK_EQUAL(17, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[0], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(10, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[1], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(6, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[2], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(25, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[3], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(31, miningMechanism.CalcMinerBlockLeftInRound(pubkeys[4], TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldReturnCorrectNumbersForAverageBlocksOnOneRound)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	for (int i = 0; i < 3; ++i) {
		CreateAndProcessBlock({}, pubkeys[0]);
		CreateAndProcessBlock({}, pubkeys[1]);
	}
	CreateAndProcessBlock({}, pubkeys[2]);
	for (int i = 0; i < 2; ++i) {
		CreateAndProcessBlock({}, pubkeys[3]);
		CreateAndProcessBlock({}, pubkeys[4]);
	}

	auto minersBlockAverage = miningMechanism.CalcMinersBlockAverageOnAllRounds(TEST_HEIGHT_THRESHOLD);

	BOOST_CHECK_EQUAL(3, minersBlockAverage[scripts[0]]);
	BOOST_CHECK_EQUAL(3, minersBlockAverage[scripts[1]]);
	BOOST_CHECK_EQUAL(1, minersBlockAverage[scripts[2]]);
	BOOST_CHECK_EQUAL(2, minersBlockAverage[scripts[3]]);
	BOOST_CHECK_EQUAL(2, minersBlockAverage[scripts[4]]);

	BOOST_CHECK_EQUAL(3, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[0], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(3, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[1], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(1, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[2], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(2, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[3], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(2, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[4], TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldReturnCorrectNumbersForAverageBlocksOnTwoRounds)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	for (int i = 0; i < 20; ++i) {
		CreateAndProcessBlock({}, pubkeys[0]);
	}
	for (int i = 0; i < 13; ++i) {
		CreateAndProcessBlock({}, pubkeys[1]);
	}
	for (int i = 0; i < 7; ++i) {
		CreateAndProcessBlock({}, pubkeys[2]);
	}
	for (int i = 0; i < 27; ++i) {
		CreateAndProcessBlock({}, pubkeys[3]);
	}
	for (int i = 0; i < 33; ++i) {
		CreateAndProcessBlock({}, pubkeys[4]);
	}

	for (int i = 0; i < 10; ++i) {
		CreateAndProcessBlock({}, pubkeys[0]);
	}
	for (int i = 0; i < 3; ++i) {
		CreateAndProcessBlock({}, pubkeys[1]);
	}
	for (int i = 0; i < 6; ++i) {
		CreateAndProcessBlock({}, pubkeys[2]);
	}
	for (int i = 0; i < 11; ++i) {
		CreateAndProcessBlock({}, pubkeys[3]);
	}
	for (int i = 0; i < 12; ++i) {
		CreateAndProcessBlock({}, pubkeys[4]);
	}

	auto minersBlockAverage = miningMechanism.CalcMinersBlockAverageOnAllRounds(TEST_HEIGHT_THRESHOLD);

	BOOST_CHECK_EQUAL(15, minersBlockAverage[scripts[0]]);
	BOOST_CHECK_EQUAL(8, minersBlockAverage[scripts[1]]);
	BOOST_CHECK_EQUAL(6.5, minersBlockAverage[scripts[2]]);
	BOOST_CHECK_EQUAL(19, minersBlockAverage[scripts[3]]);
	BOOST_CHECK_EQUAL(22.5, minersBlockAverage[scripts[4]]);

	BOOST_CHECK_EQUAL(15, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[0], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(8, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[1], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(6.5, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[2], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(19, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[3], TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(22.5, miningMechanism.CalcMinerBlockAverageOnAllRounds(pubkeys[4], TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldCanMineReturnTrueIfMinerIsNotSaturated)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	CreateAndProcessBlock({}, pubkeys[0]);
	BOOST_CHECK(miningMechanism.CanMine(pubkeys[0], prepareInvalidBlock(), TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldCanMineReturnFalseIfMinerIsSaturatedAndRoundIsClosed)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	for (int i = 0; i < 20; ++i) {
		CreateAndProcessBlock({}, pubkeys[0]);
	}

	BOOST_CHECK(!miningMechanism.CanMine(pubkeys[0], prepareInvalidBlock(), TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldCanMineReturnTrueIfRoundIsOpenBySaturatedNetworkPower)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	for (int i = 0; i < 20; ++i) {
		CreateAndProcessBlock({}, pubkeys[0]);
	}

	BOOST_CHECK(!miningMechanism.CanMine(pubkeys[0], prepareInvalidBlock(), TEST_HEIGHT_THRESHOLD));

	for (int i = 0; i < 33; ++i) {
		CreateAndProcessBlock({}, pubkeys[4]);
	}

	BOOST_CHECK(miningMechanism.CanMine(pubkeys[0], prepareInvalidBlock(), TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldCanMineReturnTrueIfRoundIsOpenByTimestampOfNewBlock)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	for (int i = 0; i < 20; ++i) {
		CreateAndProcessBlock({}, pubkeys[0]);
	}

	BOOST_CHECK(miningMechanism.CanMine(pubkeys[0], prepareValidBlock(), TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldCanMineReturnTrueIfRoundIsOpenByTimestampOfPreviousBlock)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	for (int i = 0; i < 18; ++i) {
		CreateAndProcessBlock({}, pubkeys[0]);
	}

	CreateAndProcessBlock({}, pubkeys[0], prepareValidBlock().nTime);
	CreateAndProcessBlock({}, pubkeys[0]);

	BOOST_CHECK(miningMechanism.CanMine(pubkeys[0], prepareInvalidBlock(), TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_SUITE_END()
