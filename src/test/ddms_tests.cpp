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

std::vector<std::string> prepareMinerLicenses() {
	std::string script{"6098d946df695b6c876b48c3e4c41528ed3a38de"};
	std::string script2{"6098d946df695b6c876b48c3e4c41528ed3a38dd"};
	std::string script3{"6098d946df695b6c876b48c3e4c41528ed3a38dc"};
	std::string script4{"6098d946df695b6c876b48c3e4c41528ed3a38db"};
	std::string script5{"6098d946df695b6c876b48c3e4c41528ed3a38da"};
	minerLicenses.PushLicense(0, 3, script);
	minerLicenses.PushLicense(0, 2, script2);
	minerLicenses.PushLicense(0, 1, script3);
	minerLicenses.PushLicense(0, 4, script4);
	minerLicenses.PushLicense(0, 5, script5);
	miningMechanism.RecalculateBlockLimits(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);

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

/** Valid in this context means that it fulfill timestamp condition for open round mining */
CBlock prepareValidBlock() {
	CBlock block;
	block.nTime = chainActive.Tip()->nTime + 5 * MAX_CLOSED_ROUND_TIME;
	return block;
}

CBlock prepareInvalidBlock() {
	CBlock block;
	block.nTime = chainActive.Tip()->nTime + 1;
	return block;
}

MinerLicenses::LicenseEntry prepareLicenseEntry() {
	return MinerLicenses::LicenseEntry{1, 1, "6098d946df695b6c876b48c3e4c41528ed3a38de"};
}

struct DdmsSetup : public TestChain100Setup {
	DdmsSetup()
		: TestChain100Setup(false) {
		minerLicenses = MinerLicenses{};
		miningMechanism = MiningMechanism{};
	}
	~DdmsSetup() = default;

	void mineEmptyBlocks(std::vector<int>noBlocks, std::vector<CScript> pubkeys) {
		assert(noBlocks.size() == pubkeys.size());
		for (int i = 0; i < noBlocks.size(); ++i)
			for (int n = 0; n < noBlocks[i]; ++n)
				CreateAndProcessBlock({}, pubkeys[i]);
		MilliSleep(100);
	}
};

BOOST_FIXTURE_TEST_SUITE(ddms_tests, DdmsSetup)

BOOST_AUTO_TEST_CASE(shouldFindRoundStartBlockNumberReturnCorrectValues)
{ // TODO: possible break down
	BOOST_CHECK_EQUAL(1, MiningUtil::FindRoundStartBlockNumber(0, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(1, MiningUtil::FindRoundStartBlockNumber(1, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(1, MiningUtil::FindRoundStartBlockNumber(2, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(1, MiningUtil::FindRoundStartBlockNumber(55, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(1, MiningUtil::FindRoundStartBlockNumber(99, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(100, MiningUtil::FindRoundStartBlockNumber(100, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(100, MiningUtil::FindRoundStartBlockNumber(151, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(100, MiningUtil::FindRoundStartBlockNumber(199, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(200, MiningUtil::FindRoundStartBlockNumber(201, TEST_HEIGHT_THRESHOLD));

	BOOST_CHECK_EQUAL(53, MiningUtil::FindRoundStartBlockNumber(32, 53));
	BOOST_CHECK_EQUAL(53, MiningUtil::FindRoundStartBlockNumber(53, 53));
	BOOST_CHECK_EQUAL(53, MiningUtil::FindRoundStartBlockNumber(99, 53));
	BOOST_CHECK_EQUAL(100, MiningUtil::FindRoundStartBlockNumber(100, 53));
}

BOOST_AUTO_TEST_CASE(shouldFindRoundEndBlockNumberReturnCorrectValues)
{ // TODO: possible break down
	mineEmptyBlocks({4}, {WDMO_SCRIPT});
	BOOST_CHECK_EQUAL(4, MiningUtil::FindRoundEndBlockNumber(4, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(4, MiningUtil::FindRoundEndBlockNumber(5, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(4, MiningUtil::FindRoundEndBlockNumber(3, TEST_HEIGHT_THRESHOLD));
	mineEmptyBlocks({95}, {WDMO_SCRIPT});
	BOOST_CHECK_EQUAL(99, MiningUtil::FindRoundEndBlockNumber(95, TEST_HEIGHT_THRESHOLD));
	mineEmptyBlocks({1}, {WDMO_SCRIPT});
	BOOST_CHECK_EQUAL(99, MiningUtil::FindRoundEndBlockNumber(95, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(99, MiningUtil::FindRoundEndBlockNumber(99, TEST_HEIGHT_THRESHOLD));
	mineEmptyBlocks({1}, {WDMO_SCRIPT});
	BOOST_CHECK_EQUAL(99, MiningUtil::FindRoundEndBlockNumber(95, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(99, MiningUtil::FindRoundEndBlockNumber(99, TEST_HEIGHT_THRESHOLD));
	mineEmptyBlocks({100}, {WDMO_SCRIPT});
	BOOST_CHECK_EQUAL(99, MiningUtil::FindRoundEndBlockNumber(99, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(199, MiningUtil::FindRoundEndBlockNumber(199, TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldFindBlockIndexReturnCorrectBlockIndex)
{
	BOOST_CHECK_EQUAL(chainActive.Tip(), MiningUtil::FindBlockIndex(chainActive.Tip()->nHeight));
	BOOST_CHECK_EQUAL(chainActive.Tip()->pprev, MiningUtil::FindBlockIndex(chainActive.Tip()->nHeight - 1));
}

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

BOOST_AUTO_TEST_CASE(shouldNotAddNewLicenseIfAlreadyExists)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 1);
	minerLicenses.HandleTx(CTransaction(lTx), 2);
	BOOST_CHECK_EQUAL(1, minerLicenses.GetLicenses().size());
	BOOST_CHECK_EQUAL(2, minerLicenses.GetLicenses()[0].hashRates.size());
}

BOOST_AUTO_TEST_CASE(shouldOnlyModifyLicenseIfAlreadyPushed)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	std::string script{"6098d946df695b6c876b48c3e4c41528ed3a38de"};

	minerLicenses.PushLicense(1, 3, script);
	minerLicenses.HandleTx(CTransaction(lTx), 2);
	auto licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(1, licenses.size());
	BOOST_CHECK_EQUAL(5, licenses[0].hashRates.back().hashRate);
}

BOOST_AUTO_TEST_CASE(shouldModifyLicenseIfAlreadyExists)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 1);
	auto licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(1, licenses[0].hashRates.size());
	BOOST_CHECK_EQUAL(5, licenses[0].hashRates.back().hashRate);

	lTx.vout[1].scriptPubKey[26] = 3; // modyfing hashrate
	minerLicenses.HandleTx(CTransaction(lTx), 2);
	licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(2, licenses[0].hashRates.size());
	BOOST_CHECK_EQUAL(3, licenses[0].hashRates.back().hashRate);
}

BOOST_AUTO_TEST_CASE(shouldNotModifyLicenseIfProvidedOlderEntry)
{
	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 2);
	auto licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(1, licenses[0].hashRates.size());
	BOOST_CHECK_EQUAL(5, licenses[0].hashRates.back().hashRate);

	lTx.vout[1].scriptPubKey[26] = 3; // modyfing hashrate
	minerLicenses.HandleTx(CTransaction(lTx), 1);
	licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(1, licenses[0].hashRates.size());
	BOOST_CHECK_EQUAL(5, licenses[0].hashRates.back().hashRate);
}

BOOST_AUTO_TEST_CASE(shouldPushLicenseIfNotExists)
{
	minerLicenses.PushLicense(1, 5, "6098d946df695b6c876b48c3e4c41528ed3a38de");
	auto licenses = minerLicenses.GetLicenses();

	BOOST_CHECK_EQUAL(1, licenses.size());
	BOOST_CHECK_EQUAL(1, licenses[0].hashRates.size());
	BOOST_CHECK_EQUAL(5, licenses[0].hashRates.back().hashRate);
}

BOOST_AUTO_TEST_CASE(shouldPushLicenseAddAnotherHashrateInfoIfExists)
{
	minerLicenses.PushLicense(0, 5, "6098d946df695b6c876b48c3e4c41528ed3a38de");
	minerLicenses.PushLicense(0, 7, "6098d946df695b6c876b48c3e4c41528ed3a38de");
	minerLicenses.PushLicense(0, 1, "6098d946df695b6c876b48c3e4c41528ed3a38de");
	minerLicenses.PushLicense(1, 10, "6098d946df695b6c876b48c3e4c41528ed3a38de");
	CreateAndProcessBlock({}, WDMO_SCRIPT);

	auto licenses = minerLicenses.GetLicenses();
	BOOST_CHECK_EQUAL(1, licenses.size());
	BOOST_CHECK_EQUAL(4, licenses[0].hashRates.size());
	BOOST_CHECK_EQUAL(1, minerLicenses.GetMinerHashrate("6098d946df695b6c876b48c3e4c41528ed3a38de", chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
	BOOST_CHECK_EQUAL(10, licenses[0].hashRates.back().hashRate);
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
	BOOST_CHECK_EQUAL(0.0f, minerLicenses.GetHashrateSum(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));

	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	minerLicenses.HandleTx(CTransaction(lTx), 0);

	lTx.vout[1].scriptPubKey[5]++; // other miner's address
	lTx.vout[1].scriptPubKey[25] = 1; // other miner's hashrate
	minerLicenses.HandleTx(CTransaction(lTx), 0);

	BOOST_CHECK_EQUAL(5 + ((1 << 8) + 5), minerLicenses.GetHashrateSum(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldReturnZeroHashrateIfMinerLicenseNotExists)
{
	std::string script{"6098d946df695b6c876b48c3e4c41528ed3a38de"};
	minerLicenses.PushLicense(1, 3, script);
	BOOST_CHECK_EQUAL(0, minerLicenses.GetMinerHashrate("ed83a3de82514c4e3c84b678c6b596fd649d8906", chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldReturnCorrectHashrateIfMinerLicenseExists)
{
	std::string script{"6098d946df695b6c876b48c3e4c41528ed3a38de"};
	minerLicenses.PushLicense(0, 3, script);
	BOOST_CHECK_EQUAL(3, minerLicenses.GetMinerHashrate("6098d946df695b6c876b48c3e4c41528ed3a38de", chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
	minerLicenses.PushLicense(10, 5, script);
	BOOST_CHECK_EQUAL(3, minerLicenses.GetMinerHashrate("6098d946df695b6c876b48c3e4c41528ed3a38de", chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldGetHashrateSumReturnCorrectValueAfterModyfingLicenseHashrate)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();
	BOOST_CHECK_EQUAL(15, minerLicenses.GetHashrateSum(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
	minerLicenses.PushLicense(1, 18, scripts[0]);
	// change shouldnt take a place until next round starts
	BOOST_CHECK_EQUAL(15, minerLicenses.GetHashrateSum(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));

	mineEmptyBlocks({20, 13, 7, 27, 33}, pubkeys);
	BOOST_CHECK_EQUAL(30, minerLicenses.GetHashrateSum(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldGetMinerHashrateReturnCorrectValueAfterAddingOrPushingNewLicense)
{
	std::string script{"6098d946df695b6c876b48c3e4c41528ed3a38dd"};
	CScript scriptPubKey = createLicensedMinerScript(); scriptPubKey[21] -= 1;
	minerLicenses.PushLicense(0, 3, script);

	mineEmptyBlocks({3}, {scriptPubKey});

	std::string script2{"6098d946df695b6c876b48c3e4c41528ed3a38dc"};
	BOOST_CHECK_EQUAL(0, minerLicenses.GetMinerHashrate(script2, chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
	minerLicenses.PushLicense(chainActive.Tip()->nHeight, 3, script2);
	BOOST_CHECK_EQUAL(0, minerLicenses.GetMinerHashrate(script2, chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));

	auto coinbaseTx = createCoinbase();
	auto lTx = createLicenseTransaction(coinbaseTx.GetHash());

	std::string script3{"6098d946df695b6c876b48c3e4c41528ed3a38de"};
	BOOST_CHECK_EQUAL(0, minerLicenses.GetMinerHashrate(script3, chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
	minerLicenses.HandleTx(CTransaction(lTx), chainActive.Tip()->nHeight);
	BOOST_CHECK_EQUAL(3, minerLicenses.GetLicenses().size());
	BOOST_CHECK_EQUAL(0, minerLicenses.GetMinerHashrate(script3, chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldGetMinerHashrateReturnCorrectValueAfterModyfingLicenseHashrate)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();
	BOOST_CHECK_EQUAL(3, minerLicenses.GetMinerHashrate(scripts[0], chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
	minerLicenses.PushLicense(1, 18, scripts[0]);
	// change shouldnt take a place until next round starts
	BOOST_CHECK_EQUAL(3, minerLicenses.GetMinerHashrate(scripts[0], chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));

	mineEmptyBlocks({20, 13, 7, 27, 33}, pubkeys);

	BOOST_CHECK_EQUAL(18, minerLicenses.GetMinerHashrate(scripts[0], chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD));
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

	BOOST_CHECK_EQUAL(20, miningMechanism.GetMinerBlockQuota(pubkeys[0]));
	BOOST_CHECK_EQUAL(13, miningMechanism.GetMinerBlockQuota(pubkeys[1]));
	BOOST_CHECK_EQUAL(7, miningMechanism.GetMinerBlockQuota(pubkeys[2]));
	BOOST_CHECK_EQUAL(27, miningMechanism.GetMinerBlockQuota(pubkeys[3]));
	BOOST_CHECK_EQUAL(33, miningMechanism.GetMinerBlockQuota(pubkeys[4]));
}

BOOST_AUTO_TEST_CASE(shouldReturnEqualNumbersForBlocksLeftInRoundIfNoBlocksWereMined)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	for (const auto& pubkey : pubkeys)
		BOOST_CHECK(miningMechanism.GetMinerBlockQuota(pubkey) == miningMechanism.GetMinerBlockLeftInRound(pubkey));
}

BOOST_AUTO_TEST_CASE(shouldReturnCorrectNumbersForBlocksLeftInRoundIfSomeBlocksWereMined)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	mineEmptyBlocks({3, 3, 1, 2, 2}, pubkeys);

	miningMechanism.RecalculateBlockLimits(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(17, miningMechanism.GetMinerBlockLeftInRound(pubkeys[0]));
	BOOST_CHECK_EQUAL(10, miningMechanism.GetMinerBlockLeftInRound(pubkeys[1]));
	BOOST_CHECK_EQUAL(6, miningMechanism.GetMinerBlockLeftInRound(pubkeys[2]));
	BOOST_CHECK_EQUAL(25, miningMechanism.GetMinerBlockLeftInRound(pubkeys[3]));
	BOOST_CHECK_EQUAL(31, miningMechanism.GetMinerBlockLeftInRound(pubkeys[4]));
}

BOOST_AUTO_TEST_CASE(shouldReturnCorrectNumbersForBlocksLeftInRoundIfAnyMinerSaturate)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	mineEmptyBlocks({13, 13, 1, 2, 2}, pubkeys);

	miningMechanism.RecalculateBlockLimits(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(7, miningMechanism.GetMinerBlockLeftInRound(pubkeys[0]));
	BOOST_CHECK_EQUAL(0, miningMechanism.GetMinerBlockLeftInRound(pubkeys[1]));
	BOOST_CHECK_EQUAL(6, miningMechanism.GetMinerBlockLeftInRound(pubkeys[2]));
	BOOST_CHECK_EQUAL(25, miningMechanism.GetMinerBlockLeftInRound(pubkeys[3]));
	BOOST_CHECK_EQUAL(31, miningMechanism.GetMinerBlockLeftInRound(pubkeys[4]));
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

	mineEmptyBlocks({20}, {pubkeys[0]});

	BOOST_CHECK(!miningMechanism.CanMine(pubkeys[0], prepareInvalidBlock(), TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldCanMineReturnTrueIfRoundIsOpenBySaturatedNetworkPower)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	mineEmptyBlocks({20}, {pubkeys[0]});

	BOOST_CHECK(!miningMechanism.CanMine(pubkeys[0], prepareInvalidBlock(), TEST_HEIGHT_THRESHOLD));

	mineEmptyBlocks({33}, {pubkeys[4]});

	BOOST_CHECK(miningMechanism.CanMine(pubkeys[0], prepareInvalidBlock(), TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldCanMineReturnTrueIfRoundIsOpenByTimestampOfNewBlock)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	mineEmptyBlocks({20}, {pubkeys[0]});

	BOOST_CHECK(miningMechanism.CanMine(pubkeys[0], prepareValidBlock(), TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldCanMineReturnTrueIfRoundIsOpenByTimestampOfPreviousBlock)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	mineEmptyBlocks({18}, {pubkeys[0]});

	CreateAndProcessBlock({}, pubkeys[0], prepareValidBlock().nTime);
	CreateAndProcessBlock({}, pubkeys[0]);

	BOOST_CHECK(miningMechanism.CanMine(pubkeys[0], prepareInvalidBlock(), TEST_HEIGHT_THRESHOLD));
}

BOOST_AUTO_TEST_CASE(shouldNotImpactBlocksQuotaInCurrentRoundByModyfingHashrate)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	mineEmptyBlocks({3, 3, 1, 2, 2}, pubkeys);

	BOOST_CHECK_EQUAL(20, miningMechanism.GetMinerBlockQuota(pubkeys[0]));
	BOOST_CHECK_EQUAL(13, miningMechanism.GetMinerBlockQuota(pubkeys[1]));
	BOOST_CHECK_EQUAL(7, miningMechanism.GetMinerBlockQuota(pubkeys[2]));
	BOOST_CHECK_EQUAL(27, miningMechanism.GetMinerBlockQuota(pubkeys[3]));
	BOOST_CHECK_EQUAL(33, miningMechanism.GetMinerBlockQuota(pubkeys[4]));

	minerLicenses.PushLicense(1, 18, "6098d946df695b6c876b48c3e4c41528ed3a38de");
	miningMechanism.RecalculateBlockLimits(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(20, miningMechanism.GetMinerBlockQuota(pubkeys[0]));
	BOOST_CHECK_EQUAL(13, miningMechanism.GetMinerBlockQuota(pubkeys[1]));
	BOOST_CHECK_EQUAL(7, miningMechanism.GetMinerBlockQuota(pubkeys[2]));
	BOOST_CHECK_EQUAL(27, miningMechanism.GetMinerBlockQuota(pubkeys[3]));
	BOOST_CHECK_EQUAL(33, miningMechanism.GetMinerBlockQuota(pubkeys[4]));

	mineEmptyBlocks({17, 10, 6, 25, 31}, pubkeys);

	// because we cannot forward test height threshold to these functions in this flow, need to execute it manually
	miningMechanism.EraseInvalidLicenses(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	miningMechanism.RecalculateBlockLimits(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(60, miningMechanism.GetMinerBlockQuota(pubkeys[0]));
	BOOST_CHECK_EQUAL(7, miningMechanism.GetMinerBlockQuota(pubkeys[1]));
	BOOST_CHECK_EQUAL(3, miningMechanism.GetMinerBlockQuota(pubkeys[2]));
	BOOST_CHECK_EQUAL(13, miningMechanism.GetMinerBlockQuota(pubkeys[3]));
	BOOST_CHECK_EQUAL(17, miningMechanism.GetMinerBlockQuota(pubkeys[4]));
}

BOOST_AUTO_TEST_CASE(shouldNotImpactBlocksLeftInCurrentRoundByModyfingHashrate)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();

	mineEmptyBlocks({3, 3, 1, 2, 2}, pubkeys);

	BOOST_CHECK_EQUAL(17, miningMechanism.GetMinerBlockLeftInRound(pubkeys[0]));
	BOOST_CHECK_EQUAL(10, miningMechanism.GetMinerBlockLeftInRound(pubkeys[1]));
	BOOST_CHECK_EQUAL(6, miningMechanism.GetMinerBlockLeftInRound(pubkeys[2]));
	BOOST_CHECK_EQUAL(25, miningMechanism.GetMinerBlockLeftInRound(pubkeys[3]));
	BOOST_CHECK_EQUAL(31, miningMechanism.GetMinerBlockLeftInRound(pubkeys[4]));

	minerLicenses.PushLicense(1, 18, "6098d946df695b6c876b48c3e4c41528ed3a38de");
	miningMechanism.RecalculateBlockLimits(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(17, miningMechanism.GetMinerBlockLeftInRound(pubkeys[0]));
	BOOST_CHECK_EQUAL(10, miningMechanism.GetMinerBlockLeftInRound(pubkeys[1]));
	BOOST_CHECK_EQUAL(6, miningMechanism.GetMinerBlockLeftInRound(pubkeys[2]));
	BOOST_CHECK_EQUAL(25, miningMechanism.GetMinerBlockLeftInRound(pubkeys[3]));
	BOOST_CHECK_EQUAL(31, miningMechanism.GetMinerBlockLeftInRound(pubkeys[4]));

	mineEmptyBlocks({17, 10, 6, 25, 30}, pubkeys);

	// because we cannot forward test height threshold to these functions in this flow, need to execute it manually
	miningMechanism.EraseInvalidLicenses(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	miningMechanism.RecalculateBlockLimits(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(60, miningMechanism.GetMinerBlockLeftInRound(pubkeys[0]));
	BOOST_CHECK_EQUAL(7, miningMechanism.GetMinerBlockLeftInRound(pubkeys[1]));
	BOOST_CHECK_EQUAL(3, miningMechanism.GetMinerBlockLeftInRound(pubkeys[2]));
	BOOST_CHECK_EQUAL(13, miningMechanism.GetMinerBlockLeftInRound(pubkeys[3]));
	BOOST_CHECK_EQUAL(17, miningMechanism.GetMinerBlockLeftInRound(pubkeys[4]));
}

BOOST_AUTO_TEST_CASE(shouldEraseInvalidLicensesRemoveLicensesWith0HashrateAssigned)
{
	auto scripts = prepareMinerLicenses();
	auto pubkeys = prepareMinerScripts();
	miningMechanism.EraseInvalidLicenses(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(5, minerLicenses.GetLicenses().size());

	minerLicenses.PushLicense(1, 0, scripts[0]);
	minerLicenses.PushLicense(1, 5, scripts[1]);

	// shouldnt erase license YET despite assigning 0 hashrate - wait for next round to start
	miningMechanism.EraseInvalidLicenses(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(5, minerLicenses.GetLicenses().size());

	mineEmptyBlocks({20, 13, 7, 27, 33}, pubkeys);

	// because we cannot forward test height threshold to these functions in this flow, need to execute it manually
	miningMechanism.EraseInvalidLicenses(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	miningMechanism.RecalculateBlockLimits(chainActive.Tip()->nHeight, TEST_HEIGHT_THRESHOLD);
	BOOST_CHECK_EQUAL(4, minerLicenses.GetLicenses().size());
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
