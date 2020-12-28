#include "../botpch.h"
#include "PlayerbotAIConfig.h"
#include "playerbot.h"
#include "RandomPlayerbotFactory.h"
#include "AccountMgr.h"
#include "SystemConfig.h"
#include "PlayerbotFactory.h"
#include "RandomItemMgr.h"

using namespace std;

INSTANTIATE_SINGLETON_1(PlayerbotAIConfig);

PlayerbotAIConfig::PlayerbotAIConfig()
{
}

template <class T>
void LoadList(string value, T &list)
{
    vector<string> ids = split(value, ',');
    for (vector<string>::iterator i = ids.begin(); i != ids.end(); i++)
    {
        uint32 id = atoi((*i).c_str());
        if (!id)
            continue;

        list.push_back(id);
    }
}

bool PlayerbotAIConfig::Initialize()
{
    sLog.outString("Initializing AI Playerbot by ike3, based on the original Playerbot by blueboy");

    if (!config.SetSource(SYSCONFDIR"aiplayerbot.conf"))
    {
        sLog.outString("AI Playerbot is Disabled. Unable to open configuration file aiplayerbot.conf");
        return false;
    }

    enabled = config.GetBoolDefault("AiPlayerbot.Enabled", true);
    if (!enabled)
    {
        sLog.outString("AI Playerbot is Disabled in aiplayerbot.conf");
        return false;
    }

    globalCoolDown = (uint32) config.GetIntDefault("AiPlayerbot.GlobalCooldown", 500);
    maxWaitForMove = config.GetIntDefault("AiPlayerbot.MaxWaitForMove", 3000);
    expireActionTime = config.GetIntDefault("AiPlayerbot.ExpireActionTime", 5000);
    dispelAuraDuration = config.GetIntDefault("AiPlayerbot.DispelAuraDuration", 7000);
    reactDelay = (uint32) config.GetIntDefault("AiPlayerbot.ReactDelay", 100);
    passiveDelay = (uint32) config.GetIntDefault("AiPlayerbot.PassiveDelay", 4000);
    repeatDelay = (uint32) config.GetIntDefault("AiPlayerbot.RepeatDelay", 5000);
    errorDelay = (uint32) config.GetIntDefault("AiPlayerbot.ErrorDelay", 5000);
    rpgDelay = (uint32) config.GetIntDefault("AiPlayerbot.RpgDelay", 3000);
    sitDelay = (uint32) config.GetIntDefault("AiPlayerbot.SitDelay", 30000);
    returnDelay = (uint32) config.GetIntDefault("AiPlayerbot.ReturnDelay", 7000);
    lootDelay = (uint32)config.GetIntDefault("AiPlayerbot.LootDelayDelay", 750);

    farDistance = config.GetFloatDefault("AiPlayerbot.FarDistance", 20.0f);
    sightDistance = config.GetFloatDefault("AiPlayerbot.SightDistance", 75.0f);
    spellDistance = config.GetFloatDefault("AiPlayerbot.SpellDistance", 22.0f);
    shootDistance = config.GetFloatDefault("AiPlayerbot.ShootDistance", 25.0f);
    healDistance = config.GetFloatDefault("AiPlayerbot.HealDistance", 20.0f);
    reactDistance = config.GetFloatDefault("AiPlayerbot.ReactDistance", 150.0f);
    grindDistance = config.GetFloatDefault("AiPlayerbot.GrindDistance", 75.0f);
    aggroDistance = config.GetFloatDefault("AiPlayerbot.AggroDistance", 22.0f);
    lootDistance = config.GetFloatDefault("AiPlayerbot.LootDistance", 15.0f);
    fleeDistance = config.GetFloatDefault("AiPlayerbot.FleeDistance", 7.5f);
    tooCloseDistance = config.GetFloatDefault("AiPlayerbot.TooCloseDistance", 5.0f);
    meleeDistance = config.GetFloatDefault("AiPlayerbot.MeleeDistance", 1.5f);
    followDistance = config.GetFloatDefault("AiPlayerbot.FollowDistance", 1.5f);
    whisperDistance = config.GetFloatDefault("AiPlayerbot.WhisperDistance", 6000.0f);
    contactDistance = config.GetFloatDefault("AiPlayerbot.ContactDistance", 0.5f);
    aoeRadius = config.GetFloatDefault("AiPlayerbot.AoeRadius", 5.0f);
    rpgDistance = config.GetFloatDefault("AiPlayerbot.RpgDistance", 80.0f);

    criticalHealth = config.GetIntDefault("AiPlayerbot.CriticalHealth", 20);
    lowHealth = config.GetIntDefault("AiPlayerbot.LowHealth", 50);
    mediumHealth = config.GetIntDefault("AiPlayerbot.MediumHealth", 70);
    almostFullHealth = config.GetIntDefault("AiPlayerbot.AlmostFullHealth", 85);
    lowMana = config.GetIntDefault("AiPlayerbot.LowMana", 15);
    mediumMana = config.GetIntDefault("AiPlayerbot.MediumMana", 40);

    randomGearLoweringChance = config.GetFloatDefault("AiPlayerbot.RandomGearLoweringChance", 0.15);
    randomBotMaxLevelChance = config.GetFloatDefault("AiPlayerbot.RandomBotMaxLevelChance", 0.15);
    randomBotRpgChance = config.GetFloatDefault("AiPlayerbot.RandomBotRpgChance", 0.35);

    iterationsPerTick = config.GetIntDefault("AiPlayerbot.IterationsPerTick", 100);

    allowGuildBots = config.GetBoolDefault("AiPlayerbot.AllowGuildBots", true);

    randomBotMapsAsString = config.GetStringDefault("AiPlayerbot.RandomBotMaps", "0,1,530,571");
    LoadList<vector<uint32> >(randomBotMapsAsString, randomBotMaps);
    LoadList<list<uint32> >(config.GetStringDefault("AiPlayerbot.RandomBotQuestItems", "6948,5175,5176,5177,5178,16309,12382,13704"), randomBotQuestItems);
    LoadList<list<uint32> >(config.GetStringDefault("AiPlayerbot.RandomBotSpellIds", "1"), randomBotSpellIds);
	LoadList<list<uint32> >(config.GetStringDefault("AiPlayerbot.PvpProhibitedZoneIds", "2255,656,2361,2362,2363,976,35,2268,3425,392,541,1446"), pvpProhibitedZoneIds);
    LoadList<list<uint32> >(config.GetStringDefault("AiPlayerbot.RandomBotQuestIds", "7848,3802,5505,6502,7761"), randomBotQuestIds);

    botAutologin = config.GetBoolDefault("AiPlayerbot.BotAutologin", false);
    randomBotAutologin = config.GetBoolDefault("AiPlayerbot.RandomBotAutologin", true);
    minRandomBots = config.GetIntDefault("AiPlayerbot.MinRandomBots", 50);
    maxRandomBots = config.GetIntDefault("AiPlayerbot.MaxRandomBots", 200);
    randomBotUpdateInterval = config.GetIntDefault("AiPlayerbot.RandomBotUpdateInterval", 60);
    randomBotCountChangeMinInterval = config.GetIntDefault("AiPlayerbot.RandomBotCountChangeMinInterval", 24 * 3600);
    randomBotCountChangeMaxInterval = config.GetIntDefault("AiPlayerbot.RandomBotCountChangeMaxInterval", 3 * 24 * 3600);
    minRandomBotInWorldTime = config.GetIntDefault("AiPlayerbot.MinRandomBotInWorldTime", 24 * 3600);
    maxRandomBotInWorldTime = config.GetIntDefault("AiPlayerbot.MaxRandomBotInWorldTime", 14 * 24 * 3600);
    minRandomBotRandomizeTime = config.GetIntDefault("AiPlayerbot.MinRandomBotRandomizeTime", 2 * 3600);
    maxRandomBotRandomizeTime = config.GetIntDefault("AiPlayerbot.MaxRandomRandomizeTime", 14 * 24 * 3600);
    minRandomBotChangeStrategyTime = config.GetIntDefault("AiPlayerbot.MinRandomBotChangeStrategyTime", 1800);
    maxRandomBotChangeStrategyTime = config.GetIntDefault("AiPlayerbot.MaxRandomBotChangeStrategyTime", 2 * 3600);
    minRandomBotReviveTime = config.GetIntDefault("AiPlayerbot.MinRandomBotReviveTime", 60);
    maxRandomBotReviveTime = config.GetIntDefault("AiPlayerbot.MaxRandomReviveTime", 300);
    randomBotTeleportDistance = config.GetIntDefault("AiPlayerbot.RandomBotTeleportDistance", 1000);
    randomBotsPerInterval = config.GetIntDefault("AiPlayerbot.RandomBotsPerInterval", 50);
    minRandomBotsPriceChangeInterval = config.GetIntDefault("AiPlayerbot.MinRandomBotsPriceChangeInterval", 2 * 3600);
    maxRandomBotsPriceChangeInterval = config.GetIntDefault("AiPlayerbot.MaxRandomBotsPriceChangeInterval", 48 * 3600);
    randomBotJoinLfg = config.GetBoolDefault("AiPlayerbot.RandomBotJoinLfg", true);
    randomBotJoinBG = config.GetBoolDefault("AiPlayerbot.RandomBotJoinBG", true);
    randomBotBracketCount = config.GetIntDefault("AiPlayerbot.RandomBotBracketCount", 3);
    logInGroupOnly = config.GetBoolDefault("AiPlayerbot.LogInGroupOnly", true);
    logValuesPerTick = config.GetBoolDefault("AiPlayerbot.LogValuesPerTick", false);
    fleeingEnabled = config.GetBoolDefault("AiPlayerbot.FleeingEnabled", true);
    summonAtInnkeepersEnabled = config.GetBoolDefault("AiPlayerbot.SummonAtInnkeepersEnabled", true);
    randomBotMinLevel = config.GetIntDefault("AiPlayerbot.RandomBotMinLevel", 1);
    randomBotMaxLevel = config.GetIntDefault("AiPlayerbot.RandomBotMaxLevel", 255);
    randomBotLoginAtStartup = config.GetBoolDefault("AiPlayerbot.RandomBotLoginAtStartup", true);
    randomBotTeleLevel = config.GetIntDefault("AiPlayerbot.RandomBotTeleLevel", 5);
    openGoSpell = config.GetIntDefault("AiPlayerbot.OpenGoSpell", 6477);

    randomChangeMultiplier = config.GetFloatDefault("AiPlayerbot.RandomChangeMultiplier", 1.0);

    randomBotCombatStrategies = config.GetStringDefault("AiPlayerbot.RandomBotCombatStrategies", "+dps assist,-threat,+custom::say");
    randomBotNonCombatStrategies = config.GetStringDefault("AiPlayerbot.RandomBotNonCombatStrategies", "+grind,+loot,+rpg,+custom::say");
    combatStrategies = config.GetStringDefault("AiPlayerbot.CombatStrategies", "+custom::say");
    nonCombatStrategies = config.GetStringDefault("AiPlayerbot.NonCombatStrategies", "+custom::say,+return");

    commandPrefix = config.GetStringDefault("AiPlayerbot.CommandPrefix", "");
    commandSeparator = config.GetStringDefault("AiPlayerbot.CommandSeparator", "\\\\");

    commandServerPort = config.GetIntDefault("AiPlayerbot.CommandServerPort", 0);
    perfMonEnabled = config.GetBoolDefault("AiPlayerbot.PerfMonEnabled", false);

    sLog.outString("---------------------------------------");
    sLog.outString("          Loading TalentSpecs          ");
    sLog.outString("---------------------------------------");
    sLog.outString();
    
    for (uint32 cls = 0; cls < MAX_CLASSES; ++cls)
    {
        classSpecs[cls] = ClassSpecs(1 << (cls - 1));
        for (uint32 spec = 0; spec < 100; ++spec)
        {
            ostringstream os; os << "AiPlayerbot.PremadeSpecName." << cls << "." << spec;
            string specName = config.GetStringDefault(os.str().c_str(), "");
            if (!specName.empty())
            {
                ostringstream os; os << "AiPlayerbot.PremadeSpecProb." << cls << "." << spec;
                int probability = config.GetIntDefault(os.str().c_str(), 100);

                TalentPath talentPath(spec, specName, probability);

                for (int level = 10; level <= 100; level++)
                {
                    ostringstream os; os << "AiPlayerbot.PremadeSpecLink." << cls << "." << spec << "." << level;
                    string specLink = config.GetStringDefault(os.str().c_str(), "");
                    specLink = specLink.substr(0, specLink.find("#", 0));;
                    specLink = specLink.substr(0, specLink.find(" ", 0));;

                    if (!specLink.empty())
                    {
                        ostringstream out;

                        //Ignore bad specs.
                        if (!classSpecs[cls].baseSpec.CheckTalentLink(specLink, &out))
                        {
                            sLog.outErrorDb("Error with premade spec link: %s", specLink.c_str());
                            sLog.outErrorDb("%s", out.str().c_str());
                            continue;
                        }

                        TalentSpec linkSpec(&classSpecs[cls].baseSpec, specLink);

                        if (!linkSpec.CheckTalents(level, &out))
                        {
                            sLog.outErrorDb("Error with premade spec: %s", specLink.c_str());
                            sLog.outErrorDb("%s", out.str().c_str());
                            continue;
                        }


                        talentPath.talentSpec.push_back(linkSpec);
                    }
                }

                //Only add paths that have atleast 1 spec.
                if(talentPath.talentSpec.size() > 0)
                    classSpecs[cls].TalentPath.push_back(talentPath);
            }
        }
    }
    

    randomBotAccountPrefix = config.GetStringDefault("AiPlayerbot.RandomBotAccountPrefix", "rndbot");
    randomBotAccountCount = config.GetIntDefault("AiPlayerbot.RandomBotAccountCount", 50);
    deleteRandomBotAccounts = config.GetBoolDefault("AiPlayerbot.DeleteRandomBotAccounts", false);
    randomBotGuildCount = config.GetIntDefault("AiPlayerbot.RandomBotGuildCount", 50);
    deleteRandomBotGuilds = config.GetBoolDefault("AiPlayerbot.DeleteRandomBotGuilds", false);
    //arena
    randomBotArenaTeamCount = config.GetIntDefault("AiPlayerbot.RandomBotArenaTeamCount", 50);
    deleteRandomBotArenaTeams = config.GetBoolDefault("AiPlayerbot.DeleteRandomBotArenaTeams", false);

    guildTaskEnabled = config.GetBoolDefault("AiPlayerbot.EnableGuildTasks", true);
    minGuildTaskChangeTime = config.GetIntDefault("AiPlayerbot.MinGuildTaskChangeTime", 3 * 24 * 3600);
    maxGuildTaskChangeTime = config.GetIntDefault("AiPlayerbot.MaxGuildTaskChangeTime", 4 * 24 * 3600);
    minGuildTaskAdvertisementTime = config.GetIntDefault("AiPlayerbot.MinGuildTaskAdvertisementTime", 60);
    maxGuildTaskAdvertisementTime = config.GetIntDefault("AiPlayerbot.MaxGuildTaskAdvertisementTime", 12 * 3600);
    minGuildTaskRewardTime = config.GetIntDefault("AiPlayerbot.MinGuildTaskRewardTime", 30);
    maxGuildTaskRewardTime = config.GetIntDefault("AiPlayerbot.MaxGuildTaskRewardTime", 120);
    guildTaskAdvertCleanupTime = config.GetIntDefault("AiPlayerbot.GuildTaskAdvertCleanupTime", 300);

    //cosmetics (by lidocain)
    randomBotShowCloak = config.GetBoolDefault("AiPlayerbot.RandomBotShowCloak", false);
    randomBotShowHelmet = config.GetBoolDefault("AiPlayerbot.RandomBotShowHelmet", false);

	//SPP switches
    enableGreet = config.GetBoolDefault("AiPlayerbot.EnableGreet", false);
	disableRandomLevels = config.GetBoolDefault("AiPlayerbot.DisableRandomLevels", false);
    playerbotsXPrate = config.GetIntDefault("AiPlayerbot.KillXPRate", 1);
    randomBotGrindAlone = config.GetIntDefault("AiPlayerbot.RandomBotGrindAlone", 0); //hidden config
    RandombotsWalkingRPG = config.GetBoolDefault("AiPlayerbot.RandombotsWalkingRPG", false);
    RandombotsWalkingRPGInDoors = config.GetBoolDefault("AiPlayerbot.RandombotsWalkingRPG.InDoors", false);
    minEnchantingBotLevel = config.GetIntDefault("AiPlayerbot.minEnchantingBotLevel", 60);
    randombotStartingLevel = config.GetIntDefault("AiPlayerbot.randombotStartingLevel", 5);
    gearscorecheck = config.GetBoolDefault("AiPlayerbot.GearScoreCheck", false);
	randomBotPreQuests = config.GetBoolDefault("AiPlayerbot.PreQuests", true);
    randomBotSayWithoutMaster = config.GetBoolDefault("AiPlayerbot.RandomBotSayWithoutMaster", false);

    //SPP automation
    autoPickReward = config.GetStringDefault("AiPlayerbot.AutoPickReward", "no");
    autoEquipUpgradeLoot = config.GetBoolDefault("AiPlayerbot.AutoEquipUpgradeLoot", false);
    syncQuestWithPlayer = config.GetBoolDefault("AiPlayerbot.SyncQuestWithPlayer", false);
    autoTrainSpells = config.GetStringDefault("AiPlayerbot.AutoTrainSpells", "no");
    autoPickTalents = config.GetStringDefault("AiPlayerbot.AutoPickTalents", "no");
    autoLearnTrainerSpells = config.GetBoolDefault("AiPlayerbot.AutoLearnTrainerSpells", false);
    autoLearnQuestSpells = config.GetBoolDefault("AiPlayerbot.AutoLearnQuestSpells", false);
    tweakValue = config.GetIntDefault("AiPlayerbot.TweakValue", 0);

    targetPosRecalcDistance = config.GetFloatDefault("AiPlayerbot.TargetPosRecalcDistance", 0.1f),
    BarGoLink::SetOutputState(config.GetBoolDefault("AiPlayerbot.ShowProgressBars", false));

    RandomPlayerbotFactory::CreateRandomBots();
    PlayerbotFactory::Init();
    sRandomItemMgr.Init();

    sLog.outString("---------------------------------------");
    sLog.outString("        AI Playerbot initialized       ");
    sLog.outString("---------------------------------------");
    sLog.outString();

    return true;
}


bool PlayerbotAIConfig::IsInRandomAccountList(uint32 id)
{
    return find(randomBotAccounts.begin(), randomBotAccounts.end(), id) != randomBotAccounts.end();
}

bool PlayerbotAIConfig::IsInRandomQuestItemList(uint32 id)
{
    return find(randomBotQuestItems.begin(), randomBotQuestItems.end(), id) != randomBotQuestItems.end();
}

bool PlayerbotAIConfig::IsInPvpProhibitedZone(uint32 id)
{
	return find(pvpProhibitedZoneIds.begin(), pvpProhibitedZoneIds.end(), id) != pvpProhibitedZoneIds.end();
}

string PlayerbotAIConfig::GetValue(string name)
{
    ostringstream out;

    if (name == "GlobalCooldown")
        out << globalCoolDown;
    else if (name == "ReactDelay")
        out << reactDelay;

    else if (name == "SightDistance")
        out << sightDistance;
    else if (name == "SpellDistance")
        out << spellDistance;
    else if (name == "ReactDistance")
        out << reactDistance;
    else if (name == "GrindDistance")
        out << grindDistance;
    else if (name == "LootDistance")
        out << lootDistance;
    else if (name == "FleeDistance")
        out << fleeDistance;

    else if (name == "CriticalHealth")
        out << criticalHealth;
    else if (name == "LowHealth")
        out << lowHealth;
    else if (name == "MediumHealth")
        out << mediumHealth;
    else if (name == "AlmostFullHealth")
        out << almostFullHealth;
    else if (name == "LowMana")
        out << lowMana;

    else if (name == "IterationsPerTick")
        out << iterationsPerTick;

    return out.str();
}

void PlayerbotAIConfig::SetValue(string name, string value)
{
    istringstream out(value, istringstream::in);

    if (name == "GlobalCooldown")
        out >> globalCoolDown;
    else if (name == "ReactDelay")
        out >> reactDelay;

    else if (name == "SightDistance")
        out >> sightDistance;
    else if (name == "SpellDistance")
        out >> spellDistance;
    else if (name == "ReactDistance")
        out >> reactDistance;
    else if (name == "GrindDistance")
        out >> grindDistance;
    else if (name == "LootDistance")
        out >> lootDistance;
    else if (name == "FleeDistance")
        out >> fleeDistance;

    else if (name == "CriticalHealth")
        out >> criticalHealth;
    else if (name == "LowHealth")
        out >> lowHealth;
    else if (name == "MediumHealth")
        out >> mediumHealth;
    else if (name == "AlmostFullHealth")
        out >> almostFullHealth;
    else if (name == "LowMana")
        out >> lowMana;

    else if (name == "IterationsPerTick")
        out >> iterationsPerTick;
}
