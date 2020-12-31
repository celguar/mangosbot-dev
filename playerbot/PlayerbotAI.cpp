#include "../botpch.h"
#include "PlayerbotMgr.h"
#include "playerbot.h"
#include <stdarg.h>

#include "AiFactory.h"

#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"
#include "strategy/values/LastMovementValue.h"
#include "strategy/actions/LogLevelAction.h"
#include "strategy/values/LastSpellCastValue.h"
#include "LootObjectStack.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotAI.h"
#include "PlayerbotFactory.h"
#include "PlayerbotSecurity.h"
#include "Group.h"
#include "Pet.h"
#include "SpellAuras.h"
#include "../ahbot/AhBot.h"
#include "GuildTaskMgr.h"
#include "PlayerbotDbStore.h"
#include "strategy/values/PositionValue.h"
#include "ServerFacade.h"

using namespace ai;
using namespace std;

vector<string>& split(const string &s, char delim, vector<string> &elems);
vector<string> split(const string &s, char delim);
char * strstri (string str1, string str2);
uint64 extractGuid(WorldPacket& packet);
std::string &trim(std::string &s);

set<string> PlayerbotAI::unsecuredCommands;

uint32 PlayerbotChatHandler::extractQuestId(string str)
{
    char* source = (char*)str.c_str();
    char* cId = ExtractKeyFromLink(&source,"Hquest");
    return cId ? atol(cId) : 0;
}

void PacketHandlingHelper::AddHandler(uint16 opcode, string handler)
{
    handlers[opcode] = handler;
}

void PacketHandlingHelper::Handle(ExternalEventHelper &helper)
{
    while (!queue.empty())
    {
        helper.HandlePacket(handlers, queue.top());
        queue.pop();
    }
}

void PacketHandlingHelper::AddPacket(const WorldPacket& packet)
{
	if (handlers.find(packet.GetOpcode()) != handlers.end())
        queue.push(WorldPacket(packet));
}


PlayerbotAI::PlayerbotAI() : PlayerbotAIBase(), bot(NULL), aiObjectContext(NULL),
    currentEngine(NULL), chatHelper(this), chatFilter(this), accountId(0), security(NULL), master(NULL), currentState(BOT_STATE_NON_COMBAT)
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
        engines[i] = NULL;
}

PlayerbotAI::PlayerbotAI(Player* bot) :
    PlayerbotAIBase(), chatHelper(this), chatFilter(this), security(bot), master(NULL)
{
	this->bot = bot;

	accountId = sObjectMgr.GetPlayerAccountIdByGUID(bot->GetObjectGuid());

    aiObjectContext = AiFactory::createAiObjectContext(bot, this);

    engines[BOT_STATE_COMBAT] = AiFactory::createCombatEngine(bot, this, aiObjectContext);
    engines[BOT_STATE_NON_COMBAT] = AiFactory::createNonCombatEngine(bot, this, aiObjectContext);
    engines[BOT_STATE_DEAD] = AiFactory::createDeadEngine(bot, this, aiObjectContext);
    currentEngine = engines[BOT_STATE_NON_COMBAT];
    currentState = BOT_STATE_NON_COMBAT;

    masterIncomingPacketHandlers.AddHandler(CMSG_GAMEOBJ_USE, "use game object");
    masterIncomingPacketHandlers.AddHandler(CMSG_AREATRIGGER, "area trigger");
    masterIncomingPacketHandlers.AddHandler(CMSG_GAMEOBJ_USE, "use game object");
    masterIncomingPacketHandlers.AddHandler(CMSG_LOOT_ROLL, "loot roll");
    masterIncomingPacketHandlers.AddHandler(CMSG_GOSSIP_HELLO, "gossip hello");
    masterIncomingPacketHandlers.AddHandler(CMSG_QUESTGIVER_HELLO, "gossip hello");
    masterIncomingPacketHandlers.AddHandler(CMSG_QUESTGIVER_COMPLETE_QUEST, "complete quest");
    masterIncomingPacketHandlers.AddHandler(CMSG_QUESTGIVER_ACCEPT_QUEST, "accept quest");
    masterIncomingPacketHandlers.AddHandler(CMSG_ACTIVATETAXI, "activate taxi");
    masterIncomingPacketHandlers.AddHandler(CMSG_ACTIVATETAXIEXPRESS, "activate taxi");
    masterIncomingPacketHandlers.AddHandler(CMSG_TAXICLEARALLNODES, "taxi done");
    masterIncomingPacketHandlers.AddHandler(CMSG_TAXICLEARNODE, "taxi done");
    masterIncomingPacketHandlers.AddHandler(CMSG_GROUP_UNINVITE_GUID, "uninvite");
    masterIncomingPacketHandlers.AddHandler(CMSG_PUSHQUESTTOPARTY, "quest share");
    masterIncomingPacketHandlers.AddHandler(CMSG_GUILD_INVITE, "guild invite");
    masterIncomingPacketHandlers.AddHandler(CMSG_BATTLEFIELD_PORT, "bg invite");    

    botOutgoingPacketHandlers.AddHandler(SMSG_BATTLEFIELD_STATUS, "bg status");
    botOutgoingPacketHandlers.AddHandler(SMSG_GROUP_INVITE, "group invite");
    botOutgoingPacketHandlers.AddHandler(BUY_ERR_NOT_ENOUGHT_MONEY, "not enough money");
    botOutgoingPacketHandlers.AddHandler(BUY_ERR_REPUTATION_REQUIRE, "not enough reputation");
    botOutgoingPacketHandlers.AddHandler(SMSG_GROUP_SET_LEADER, "group set leader");
    botOutgoingPacketHandlers.AddHandler(SMSG_FORCE_RUN_SPEED_CHANGE, "check mount state");
    botOutgoingPacketHandlers.AddHandler(SMSG_RESURRECT_REQUEST, "resurrect request");
    botOutgoingPacketHandlers.AddHandler(SMSG_INVENTORY_CHANGE_FAILURE, "cannot equip");
    botOutgoingPacketHandlers.AddHandler(SMSG_TRADE_STATUS, "trade status");
    botOutgoingPacketHandlers.AddHandler(SMSG_LOOT_RESPONSE, "loot response");
    botOutgoingPacketHandlers.AddHandler(SMSG_QUESTUPDATE_ADD_KILL, "quest objective completed");
    botOutgoingPacketHandlers.AddHandler(SMSG_ITEM_PUSH_RESULT, "item push result");
    botOutgoingPacketHandlers.AddHandler(SMSG_PARTY_COMMAND_RESULT, "party command");
    botOutgoingPacketHandlers.AddHandler(SMSG_LEVELUP_INFO, "levelup");
    botOutgoingPacketHandlers.AddHandler(SMSG_LOG_XPGAIN, "xpgain");
    
    
#ifdef MANGOS
    botOutgoingPacketHandlers.AddHandler(SMSG_CAST_FAILED, "cast failed");
#endif
#ifdef CMANGOS
    botOutgoingPacketHandlers.AddHandler(SMSG_CAST_RESULT, "cast failed");
#endif
    botOutgoingPacketHandlers.AddHandler(SMSG_DUEL_REQUESTED, "duel requested");
    botOutgoingPacketHandlers.AddHandler(SMSG_INVENTORY_CHANGE_FAILURE, "inventory change failure");

    masterOutgoingPacketHandlers.AddHandler(SMSG_PARTY_COMMAND_RESULT, "party command");
    masterOutgoingPacketHandlers.AddHandler(MSG_RAID_READY_CHECK, "ready check");
    masterOutgoingPacketHandlers.AddHandler(MSG_RAID_READY_CHECK_FINISHED, "ready check finished");
}

PlayerbotAI::~PlayerbotAI()
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
    {
        if (engines[i])
            delete engines[i];
    }

    if (aiObjectContext)
        delete aiObjectContext;
}

void PlayerbotAI::UpdateAIInternal(uint32 elapsed)
{
    if (bot->IsBeingTeleported() || !bot->IsInWorld())
        return;

    PerformanceMonitorOperation *pmo = sPerformanceMonitor.start(PERF_MON_TOTAL, "PlayerbotAI::UpdateAIInternal");
    ExternalEventHelper helper(aiObjectContext);
    list<ChatCommandHolder> delayed;
    while (!chatCommands.empty())
    {
        ChatCommandHolder holder = chatCommands.front();
        time_t checkTime = holder.GetTime();
        if (checkTime && time(0) < checkTime)
        {
            delayed.push_back(holder);
            chatCommands.pop();
            continue;
        }
        string command = holder.GetCommand();
        Player* owner = holder.GetOwner();
        if (!helper.ParseChatCommand(command, owner) && holder.GetType() == CHAT_MSG_WHISPER)
        {
            ostringstream out; out << "Unknown command " << command;
            TellMaster(out);
            helper.ParseChatCommand("help");
        }
        chatCommands.pop();
    }

    for (list<ChatCommandHolder>::iterator i = delayed.begin(); i != delayed.end(); ++i)
    {
        chatCommands.push(*i);
    }

    botOutgoingPacketHandlers.Handle(helper);
    masterIncomingPacketHandlers.Handle(helper);
    masterOutgoingPacketHandlers.Handle(helper);

	DoNextAction();
	if (pmo) pmo->finish();
}

void PlayerbotAI::HandleTeleportAck()
{
	bot->GetMotionMaster()->Clear(true);
	bot->InterruptMoving(1);
	if (bot->IsBeingTeleportedNear())
	{
		WorldPacket p = WorldPacket(MSG_MOVE_TELEPORT_ACK, 8 + 4 + 4);
        p << bot->GetObjectGuid();
		p << (uint32) 0; // supposed to be flags? not used currently
		p << (uint32) time(0); // time - not currently used
        bot->GetSession()->HandleMoveTeleportAckOpcode(p);
	}
	else if (bot->IsBeingTeleportedFar())
	{
        bot->GetSession()->HandleMoveWorldportAckOpcode();
		SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
	}
}

void PlayerbotAI::Reset()
{
    if (bot->IsTaxiFlying())
        return;

    currentEngine = engines[BOT_STATE_NON_COMBAT];
    nextAICheckDelay = 0;
    whispers.clear();

    aiObjectContext->GetValue<Unit*>("old target")->Set(NULL);
    aiObjectContext->GetValue<Unit*>("current target")->Set(NULL);
    aiObjectContext->GetValue<ObjectGuid>("pull target")->Set(ObjectGuid());
    aiObjectContext->GetValue<LootObject>("loot target")->Set(LootObject());
    aiObjectContext->GetValue<uint32>("lfg proposal")->Set(0);

    LastSpellCast & lastSpell = aiObjectContext->GetValue<LastSpellCast& >("last spell cast")->Get();
    lastSpell.Reset();

    aiObjectContext->GetValue<LastMovement& >("last movement")->Get().Set(NULL);
    aiObjectContext->GetValue<LastMovement& >("last area trigger")->Get().Set(NULL);
    aiObjectContext->GetValue<LastMovement& >("last taxi")->Get().Set(NULL);

    bot->GetMotionMaster()->Clear();
#ifdef MANGOS
    bot->m_taxi.ClearTaxiDestinations();
#endif
    InterruptSpell();

    for (int i = 0 ; i < BOT_STATE_MAX; i++)
    {
        engines[i]->Init();
    }
}

map<string,ChatMsg> chatMap;

bool PlayerbotAI::IsAllowedCommand(string text)
{
    if (unsecuredCommands.empty())
    {
        unsecuredCommands.insert("who");
        unsecuredCommands.insert("wts");
        unsecuredCommands.insert("sendmail");
    }

    for (set<string>::iterator i = unsecuredCommands.begin(); i != unsecuredCommands.end(); ++i)
    {
        if (text.find(*i) == 0)
        {
            return true;
        }
    }

    return false;
}

void PlayerbotAI::HandleCommand(uint32 type, const string& text, Player& fromPlayer)
{
    if (!GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, type != CHAT_MSG_WHISPER, &fromPlayer))
        return;

    if (type == CHAT_MSG_ADDON)
        return;

    if (text.find(sPlayerbotAIConfig.commandSeparator) != string::npos)
    {
        vector<string> commands;
        split(commands, text, sPlayerbotAIConfig.commandSeparator.c_str());
        for (vector<string>::iterator i = commands.begin(); i != commands.end(); ++i)
        {
            HandleCommand(type, *i, fromPlayer);
        }
        return;
    }

    string filtered = text;
    if (!sPlayerbotAIConfig.commandPrefix.empty())
    {
        if (filtered.find(sPlayerbotAIConfig.commandPrefix) != 0)
            return;

        filtered = filtered.substr(sPlayerbotAIConfig.commandPrefix.size());
    }

    if (chatMap.empty())
    {
        chatMap["#w "] = CHAT_MSG_WHISPER;
        chatMap["#p "] = CHAT_MSG_PARTY;
        chatMap["#r "] = CHAT_MSG_RAID;
        chatMap["#a "] = CHAT_MSG_ADDON;
        chatMap["#g "] = CHAT_MSG_GUILD;
    }
    currentChat = pair<ChatMsg, time_t>(CHAT_MSG_WHISPER, 0);
    for (map<string,ChatMsg>::iterator i = chatMap.begin(); i != chatMap.end(); ++i)
    {
        if (filtered.find(i->first) == 0)
        {
            filtered = filtered.substr(3);
            currentChat = pair<ChatMsg, time_t>(i->second, time(0) + 2);
            break;
        }
    }

    filtered = chatFilter.Filter(trim((string&)filtered));
    if (filtered.empty())
        return;

    if (filtered.substr(0, 6) == "debug ")
    {
        string response = HandleRemoteCommand(filtered.substr(6));
        WorldPacket data;
        ChatHandler::BuildChatPacket(data, CHAT_MSG_ADDON, response.c_str(), LANG_ADDON,
                CHAT_TAG_NONE, bot->GetObjectGuid(), bot->GetName());
        sServerFacade.SendPacket(&fromPlayer, data);
        return;
    }

    if (!IsAllowedCommand(filtered) && !GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, type != CHAT_MSG_WHISPER, &fromPlayer))
        return;

    if (type == CHAT_MSG_RAID_WARNING && filtered.find(bot->GetName()) != string::npos && filtered.find("award") == string::npos)
    {
        ChatCommandHolder cmd("warning", &fromPlayer, type);
        chatCommands.push(cmd);
        return;
    }

    if ((filtered.size() > 2 && filtered.substr(0, 2) == "d ") || (filtered.size() > 3 && filtered.substr(0, 3) == "do "))
    {
        std::string action = filtered.substr(filtered.find(" ") + 1);
        DoSpecificAction(action);
    }
    else if (type != CHAT_MSG_WHISPER && filtered.size() > 6 && filtered.substr(0, 6) == "queue ")
    {
        std::string remaining = filtered.substr(filtered.find(" ") + 1);
        int index = 1;
        Group* group = bot->GetGroup();
        if (group)
        {
            for (GroupReference *ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                if (ref->getSource() == master)
                    continue;

                if (ref->getSource() == bot)
                    break;

                index++;
            }
        }
        ChatCommandHolder cmd(remaining, &fromPlayer, type, time(0) + index);
        chatCommands.push(cmd);
    }
    else if (filtered == "reset")
    {
        Reset();
    }
    else
    {
        ChatCommandHolder cmd(filtered, &fromPlayer, type);
        chatCommands.push(cmd);
    }
}

void PlayerbotAI::HandleBotOutgoingPacket(const WorldPacket& packet)
{
	switch (packet.GetOpcode())
	{
	case SMSG_SPELL_FAILURE:
	{
		WorldPacket p(packet);
		p.rpos(0);
		ObjectGuid casterGuid;
        p >> casterGuid.ReadAsPacked();
		if (casterGuid != bot->GetObjectGuid())
			return;

		uint32 spellId;
		p >> spellId;
		SpellInterrupted(spellId);
		return;
	}
	case SMSG_SPELL_DELAYED:
	{
		WorldPacket p(packet);
		p.rpos(0);
		ObjectGuid casterGuid;
        p >> casterGuid.ReadAsPacked();

		if (casterGuid != bot->GetObjectGuid())
			return;

		uint32 delaytime;
		p >> delaytime;
		if (delaytime <= 1000)
			IncreaseNextCheckDelay(delaytime);
		return;
	}
	default:
		botOutgoingPacketHandlers.AddPacket(packet);
	}
}

void PlayerbotAI::SpellInterrupted(uint32 spellid)
{
    LastSpellCast& lastSpell = aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get();
    if (!spellid || lastSpell.id != spellid)
        return;

    time_t now = time(0);
    if (now <= lastSpell.time)
        return;

    uint32 castTimeSpent = 1000 * (now - lastSpell.time);
    int32 globalCooldown = CalculateGlobalCooldown(lastSpell.id);
    if (castTimeSpent < globalCooldown)
        SetNextCheckDelay(globalCooldown - castTimeSpent);
    else
        SetNextCheckDelay(sPlayerbotAIConfig.reactDelay);

    lastSpell.id = 0;
}

int32 PlayerbotAI::CalculateGlobalCooldown(uint32 spellid)
{
    if (!spellid)
        return 0;

#ifdef MANGOS
    if (bot->HasSpellCooldown(spellid))
        return sPlayerbotAIConfig.globalCoolDown;

    return sPlayerbotAIConfig.reactDelay;
#endif

#ifdef CMANGOS
    return sPlayerbotAIConfig.globalCoolDown;
#endif
}

void PlayerbotAI::HandleMasterIncomingPacket(const WorldPacket& packet)
{
    masterIncomingPacketHandlers.AddPacket(packet);
}

void PlayerbotAI::HandleMasterOutgoingPacket(const WorldPacket& packet)
{
    masterOutgoingPacketHandlers.AddPacket(packet);
}

void PlayerbotAI::ChangeEngine(BotState type)
{
    Engine* engine = engines[type];

    if (currentEngine != engine)
    {
        currentEngine = engine;
        currentState = type;
        ReInitCurrentEngine();

        switch (type)
        {
        case BOT_STATE_COMBAT:
            sLog.outDebug( "=== %s COMBAT ===", bot->GetName());
            break;
        case BOT_STATE_NON_COMBAT:
            sLog.outDebug( "=== %s NON-COMBAT ===", bot->GetName());
            break;
        case BOT_STATE_DEAD:
            sLog.outDebug( "=== %s DEAD ===", bot->GetName());
            break;
        }
    }
}

void PlayerbotAI::DoNextAction()
{
    if (bot->IsBeingTeleported() || (GetMaster() && GetMaster()->IsBeingTeleported()))
    {
        SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
        return;
    }

    if (bot->IsTaxiFlying()
#ifdef MANGOSBOT_ZERO
            || bot->IsFlying()
#endif
            )
    {
        SetNextCheckDelay(sPlayerbotAIConfig.passiveDelay);
        return;
    }

    currentEngine->DoNextAction(NULL);

    if (currentEngine != engines[BOT_STATE_DEAD] && !sServerFacade.IsAlive(bot))
        ChangeEngine(BOT_STATE_DEAD);

    if (currentEngine == engines[BOT_STATE_DEAD] && sServerFacade.IsAlive(bot))
        ChangeEngine(BOT_STATE_NON_COMBAT);

    Group *group = bot->GetGroup();
    //if (!master && group)
    if (!master && group && !bot->InBattleGround())
    {
        for (GroupReference *gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* member = gref->getSource();
            PlayerbotAI* ai = bot->GetPlayerbotAI();
            if (member && member->IsInWorld() && !member->GetPlayerbotAI() && (!master || master->GetPlayerbotAI()))
            {
                ai->SetMaster(member);
                ai->ResetStrategies();
                ai->ChangeStrategy("-rpg", BOT_STATE_NON_COMBAT);
                ai->ChangeStrategy("-grind", BOT_STATE_NON_COMBAT);
                ai->TellMaster("Hello");
                break;
            }
        }
    }

    if (master)
	{
		if (master->m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE)) bot->m_movementInfo.AddMovementFlag(MOVEFLAG_WALK_MODE);
		else bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_WALK_MODE);
	}
	else if (bot->m_movementInfo.HasMovementFlag(MOVEFLAG_WALK_MODE)) bot->m_movementInfo.RemoveMovementFlag(MOVEFLAG_WALK_MODE);
}

void PlayerbotAI::ReInitCurrentEngine()
{
    InterruptSpell();
    currentEngine->Init();
}

void PlayerbotAI::ChangeStrategy(string names, BotState type)
{
    Engine* e = engines[type];
    if (!e)
        return;

    e->ChangeStrategy(names);
}

void PlayerbotAI::ClearStrategies(BotState type)
{
    Engine* e = engines[type];
    if (!e)
        return;

    e->removeAllStrategies();
}

list<string> PlayerbotAI::GetStrategies(BotState type)
{
    Engine* e = engines[type];
    if (!e)
        return list<string>();

    return e->GetStrategies();
}

bool PlayerbotAI::DoSpecificAction(string name, Event event, bool silent)
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
    {
        ostringstream out;
        ActionResult res = engines[i]->ExecuteAction(name, event);
        switch (res)
        {
        case ACTION_RESULT_UNKNOWN:
            continue;
        case ACTION_RESULT_OK:
            PlaySound(TEXTEMOTE_NOD);
            return true;
        case ACTION_RESULT_IMPOSSIBLE:
            out << name << ": impossible";
            if (!silent)
            {
                TellError(out.str());
                PlaySound(TEXTEMOTE_NO);
            }
            return false;
        case ACTION_RESULT_USELESS:
            out << name << ": useless";
            if (!silent)
            {
                TellError(out.str());
                PlaySound(TEXTEMOTE_NO);
            }
            return false;
        case ACTION_RESULT_FAILED:
            if (!silent)
            {
                out << name << ": failed";
                TellError(out.str());
            }
            return false;
        }
    }
    if (!silent)
    {
        ostringstream out;
        out << name << ": unknown action";
        TellError(out.str());
    }

    return false;
}

bool PlayerbotAI::PlaySound(uint32 emote)
{
    if (EmotesTextSoundEntry const* soundEntry = FindTextSoundEmoteFor(emote, bot->getRace(), bot->getGender()))
    {
        bot->PlayDistanceSound(soundEntry->SoundId);
        return true;
    }

    return false;
}

//thesawolf - emotion responses
void PlayerbotAI::ReceiveEmote(Player* player, uint32 emote)
{
    // thesawolf - lets clear any running emotes first
    bot->HandleEmoteCommand(EMOTE_ONESHOT_NONE);
    switch (emote)
    {
    case TEXTEMOTE_BONK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
        break;
    case TEXTEMOTE_SALUTE:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
        break;
    //case TEXTEMOTE_WAIT:
    //    //SetBotCommandState(COMMAND_STAY);
    //    bot->Say("Fine.. I'll stay right here..", LANG_UNIVERSAL);
    //    break;
    case TEXTEMOTE_BECKON:
    //case TEXTEMOTE_FOLLOW:
        //SetBotCommandState(COMMAND_FOLLOW, true);
        bot->Say("Wherever you go, I'll follow..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_WAVE:
    case TEXTEMOTE_GREET:
    case TEXTEMOTE_HAIL:
    case TEXTEMOTE_HELLO:
    case TEXTEMOTE_WELCOME:
    case TEXTEMOTE_INTRODUCE:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
        bot->Say("Hey there!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_DANCE:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_DANCE);
        bot->Say("Shake what your mama gave you!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_FLIRT:
    case TEXTEMOTE_KISS:
    case TEXTEMOTE_HUG:
    case TEXTEMOTE_BLUSH:
    case TEXTEMOTE_SMILE:
    case TEXTEMOTE_LOVE:
    //case TEXTEMOTE_HOLDHAND:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_SHY);
        bot->Say("Awwwww...", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_FLEX:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_APPLAUD);
        bot->Say("Hercules! Hercules!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_ANGRY:
    //case TEXTEMOTE_FACEPALM:
    case TEXTEMOTE_GLARE:
    case TEXTEMOTE_BLAME:
    //case TEXTEMOTE_FAIL:
    //case TEXTEMOTE_REGRET:
    //case TEXTEMOTE_SCOLD:
    //case TEXTEMOTE_CROSSARMS:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
        bot->Say("Did I do thaaaaat?", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_FART:
    case TEXTEMOTE_BURP:
    case TEXTEMOTE_GASP:
    case TEXTEMOTE_NOSEPICK:
    case TEXTEMOTE_SNIFF:
    case TEXTEMOTE_STINK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
        bot->Say("Wasn't me! Just sayin'..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_JOKE:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
        bot->Say("Oh.. was I not supposed to laugh so soon?", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_CHICKEN:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_RUDE);
        bot->Say("We'll see who's chicken soon enough!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_APOLOGIZE:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
        bot->Say("You damn right you're sorry!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_APPLAUD:
    case TEXTEMOTE_CLAP:
    case TEXTEMOTE_CONGRATULATE:
    case TEXTEMOTE_HAPPY:
    //case TEXTEMOTE_GOLFCLAP:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
        bot->Say("Thank you.. Thank you.. I'm here all week.", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_BEG:
    case TEXTEMOTE_GROVEL:
    case TEXTEMOTE_PLEAD:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
        bot->Say("Beg all you want.. I have nothing for you.", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_BITE:
    case TEXTEMOTE_POKE:
    case TEXTEMOTE_SCRATCH:
    //case TEXTEMOTE_PINCH:
    //case TEXTEMOTE_PUNCH:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
        bot->Yell("OUCH! Dammit, that hurt!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_BORED:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
        bot->Say("My job description doesn't include entertaining you..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_BOW:
    case TEXTEMOTE_CURTSEY:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
        break;
    case TEXTEMOTE_BRB:
    case TEXTEMOTE_SIT:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_EAT);
        bot->Say("Looks like time for an AFK break..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_AGREE:
    case TEXTEMOTE_NOD:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
        bot->Say("At least SOMEONE agrees with me!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_AMAZE:
    case TEXTEMOTE_COWER:
    case TEXTEMOTE_CRINGE:
    case TEXTEMOTE_EYE:
    case TEXTEMOTE_KNEEL:
    case TEXTEMOTE_PEER:
    case TEXTEMOTE_SURRENDER:
    case TEXTEMOTE_PRAISE:
    case TEXTEMOTE_SCARED:
    case TEXTEMOTE_COMMEND:
    //case TEXTEMOTE_AWE:
    //case TEXTEMOTE_JEALOUS:
    //case TEXTEMOTE_PROUD:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_FLEX);
        bot->Say("Yes, Yes. I know I'm amazing..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_BLEED:
    case TEXTEMOTE_MOURN:
    case TEXTEMOTE_FLOP:
    //case TEXTEMOTE_FAINT:
    //case TEXTEMOTE_PULSE:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
        bot->Yell("MEDIC! Stat!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_BLINK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_KICK);
        bot->Say("What? You got something in your eye?", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_BOUNCE:
    case TEXTEMOTE_BARK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
        bot->Say("Who's a good doggy? You're a good doggy!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_BYE:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_WAVE);
        bot->Say("Umm.... wait! Where are you going?!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_CACKLE:
    case TEXTEMOTE_LAUGH:
    case TEXTEMOTE_CHUCKLE:
    case TEXTEMOTE_GIGGLE:
    case TEXTEMOTE_GUFFAW:
    case TEXTEMOTE_ROFL:
    case TEXTEMOTE_SNICKER:
    //case TEXTEMOTE_SNORT:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
        bot->Say("Wait... what are we laughing at again?", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_CONFUSED:
    case TEXTEMOTE_CURIOUS:
    case TEXTEMOTE_FIDGET:
    case TEXTEMOTE_FROWN:
    case TEXTEMOTE_SHRUG:
    case TEXTEMOTE_SIGH:
    case TEXTEMOTE_STARE:
    case TEXTEMOTE_TAP:
    case TEXTEMOTE_SURPRISED:
    case TEXTEMOTE_WHINE:
    case TEXTEMOTE_BOGGLE:
    case TEXTEMOTE_LOST:
    case TEXTEMOTE_PONDER:
    case TEXTEMOTE_SNUB:
    case TEXTEMOTE_SERIOUS:
    case TEXTEMOTE_EYEBROW:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
        bot->Say("Don't look at  me.. I just work here", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_COUGH:
    case TEXTEMOTE_DROOL:
    case TEXTEMOTE_SPIT:
    case TEXTEMOTE_LICK:
    case TEXTEMOTE_BREATH:
    //case TEXTEMOTE_SNEEZE:
    //case TEXTEMOTE_SWEAT:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
        bot->Say("Ewww! Keep your nasty germs over there!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_CRY:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
        bot->Say("Don't you start crying or it'll make me start crying!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_CRACK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
        bot->Say("It's clobbering time!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_EAT:
    case TEXTEMOTE_DRINK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_EAT);
        bot->Say("I hope you brought enough for the whole class...", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_GLOAT:
    case TEXTEMOTE_MOCK:
    case TEXTEMOTE_TEASE:
    case TEXTEMOTE_EMBARRASS:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
        bot->Say("Doesn't mean you need to be an ass about it..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_HUNGRY:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_EAT);
        bot->Say("What? You want some of this?", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_LAYDOWN:
    case TEXTEMOTE_TIRED:
    case TEXTEMOTE_YAWN:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_KNEEL);
        bot->Say("Is it break time already?", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_MOAN:
    case TEXTEMOTE_MOON:
    case TEXTEMOTE_SEXY:
    case TEXTEMOTE_SHAKE:
    case TEXTEMOTE_WHISTLE:
    case TEXTEMOTE_CUDDLE:
    case TEXTEMOTE_PURR:
    case TEXTEMOTE_SHIMMY:
    case TEXTEMOTE_SMIRK:
    case TEXTEMOTE_WINK:
    //case TEXTEMOTE_CHARM:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
        bot->Say("Keep it in your pants, boss..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_NO:
    case TEXTEMOTE_VETO:
    case TEXTEMOTE_DISAGREE:
    case TEXTEMOTE_DOUBT:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
        bot->Say("Aww.... why not?!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_PANIC:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
        bot->Say("Now is NOT the time to panic!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_POINT:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
        bot->Say("What?! I can do that TOO!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_RUDE:
    case TEXTEMOTE_RASP:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_RUDE);
        bot->Say("Right back at you, bub!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_ROAR:
    case TEXTEMOTE_THREATEN:
    case TEXTEMOTE_CALM:
    case TEXTEMOTE_DUCK:
    case TEXTEMOTE_TAUNT:
    case TEXTEMOTE_PITY:
    case TEXTEMOTE_GROWL:
    //case TEXTEMOTE_TRAIN:
    //case TEXTEMOTE_INCOMING:
    //case TEXTEMOTE_CHARGE:
    //case TEXTEMOTE_FLEE:
    //case TEXTEMOTE_ATTACKMYTARGET:
    case TEXTEMOTE_OPENFIRE:
    case TEXTEMOTE_ENCOURAGE:
    case TEXTEMOTE_ENEMY:
    //case TEXTEMOTE_CHALLENGE:
    //case TEXTEMOTE_REVENGE:
    //case TEXTEMOTE_SHAKEFIST:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
        bot->Yell("RAWR!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_TALK:
    case TEXTEMOTE_TALKEX:
    case TEXTEMOTE_TALKQ:
    case TEXTEMOTE_LISTEN:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
        bot->Say("Blah Blah Blah Yakety Smackety..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_THANK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_BOW);
        bot->Say("You are quite welcome!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_VICTORY:
    case TEXTEMOTE_CHEER:
    case TEXTEMOTE_TOAST:
    //case TEXTEMOTE_HIGHFIVE:
    //case TEXTEMOTE_DING:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_CHEER);
        bot->Say("Yay!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_COLD:
    case TEXTEMOTE_SHIVER:
    case TEXTEMOTE_THIRSTY:
    //case TEXTEMOTE_OOM:
    //case TEXTEMOTE_HEALME:
    //case TEXTEMOTE_POUT:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
        bot->Say("And what exactly am I supposed to do about that?", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_COMFORT:
    case TEXTEMOTE_SOOTHE:
    case TEXTEMOTE_PAT:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
        bot->Say("Thanks...", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_INSULT:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
        bot->Say("You hurt my feelings..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_JK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
        bot->Say("You.....", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_RAISE:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
        bot->Say("Yes.. you.. at the back of the class..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_READY:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_SALUTE);
        bot->Say("Ready here, too!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_SHOO:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_KICK);
        bot->Say("Shoo yourself!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_SLAP:
    //case TEXTEMOTE_SMACK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_CRY);
        bot->Say("What did I do to deserve that?", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_STAND:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_NONE);
        bot->Say("What? Break time's over? Fine..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_TICKLE:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
        bot->Say("Hey! Stop that!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_VIOLIN:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
        bot->Say("Har Har.. very funny..", LANG_UNIVERSAL);
        break;
    //case TEXTEMOTE_HELPME:
    //    bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
    //    bot->Yell("Quick! Someone HELP!", LANG_UNIVERSAL);
    //    break;
    case TEXTEMOTE_GOODLUCK:
    //case TEXTEMOTE_LUCK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
        bot->Say("Thanks... I'll need it..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_BRANDISH:
    //case TEXTEMOTE_MERCY:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_BEG);
        bot->Say("Please don't kill me!", LANG_UNIVERSAL);
        break;
    /*case TEXTEMOTE_BADFEELING:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
        bot->Say("I'm just waiting for the ominous music now...", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_MAP:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
        bot->Say("Noooooooo.. you just couldn't ask for directions, huh?", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_IDEA:
    case TEXTEMOTE_THINK:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
        bot->Say("Oh boy.. another genius idea...", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_OFFER:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_NO);
        bot->Say("No thanks.. I had some back at the last village", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_PET:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);
        bot->Say("Do I look like a dog to you?!", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_ROLLEYES:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
        bot->Say("Keep doing that and I'll roll those eyes right out of your head..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_SING:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_APPLAUD);
        bot->Say("Lovely... just lovely..", LANG_UNIVERSAL);
        break;
    case TEXTEMOTE_COVEREARS:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_EXCLAMATION);
        bot->Yell("You think that's going to help you?!", LANG_UNIVERSAL);
        break;*/
    default:
        bot->HandleEmoteCommand(EMOTE_ONESHOT_QUESTION);
        bot->Say("Mmmmmkaaaaaay...", LANG_UNIVERSAL);
        break;
    }
    return;
}

bool PlayerbotAI::ContainsStrategy(StrategyType type)
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
    {
        if (engines[i]->ContainsStrategy(type))
            return true;
    }
    return false;
}

bool PlayerbotAI::HasStrategy(string name, BotState type)
{
    return engines[type]->HasStrategy(name);
}

void PlayerbotAI::ResetStrategies(bool load)
{
    for (int i = 0 ; i < BOT_STATE_MAX; i++)
        engines[i]->removeAllStrategies();

    AiFactory::AddDefaultCombatStrategies(bot, this, engines[BOT_STATE_COMBAT]);
    AiFactory::AddDefaultNonCombatStrategies(bot, this, engines[BOT_STATE_NON_COMBAT]);
    AiFactory::AddDefaultDeadStrategies(bot, this, engines[BOT_STATE_DEAD]);
    if (load) sPlayerbotDbStore.Load(this);
}

bool PlayerbotAI::IsRanged(Player* player)
{
    PlayerbotAI* botAi = player->GetPlayerbotAI();
    if (botAi && !player->InBattleGround())
        return botAi->ContainsStrategy(STRATEGY_TYPE_RANGED);

    switch (player->getClass())
    {
    case CLASS_PALADIN:
    case CLASS_WARRIOR:
    case CLASS_ROGUE:
        return false;
    case CLASS_DRUID:
        return !HasAnyAuraOf(player, "cat form", "bear form", "dire bear form", NULL);
    }
    return true;
}

bool PlayerbotAI::IsTank(Player* player)
{
    PlayerbotAI* botAi = player->GetPlayerbotAI();
    if (botAi)
        return botAi->ContainsStrategy(STRATEGY_TYPE_TANK);

    switch (player->getClass())
    {
    case CLASS_PALADIN:
    case CLASS_WARRIOR:
        return true;
    case CLASS_DRUID:
        return HasAnyAuraOf(player, "bear form", "dire bear form", NULL);
    }
    return false;
}

bool PlayerbotAI::IsHeal(Player* player)
{
    PlayerbotAI* botAi = player->GetPlayerbotAI();
    if (botAi)
        return botAi->ContainsStrategy(STRATEGY_TYPE_HEAL);

    switch (player->getClass())
    {
    case CLASS_PRIEST:
        return true;
    case CLASS_DRUID:
        return HasAnyAuraOf(player, "tree of life form", NULL);
    }
    return false;
}



namespace MaNGOS
{

    class UnitByGuidInRangeCheck
    {
    public:
        UnitByGuidInRangeCheck(WorldObject const* obj, ObjectGuid guid, float range) : i_obj(obj), i_range(range), i_guid(guid) {}
        WorldObject const& GetFocusObject() const { return *i_obj; }
        bool operator()(Unit* u)
        {
            return u->GetObjectGuid() == i_guid && i_obj->IsWithinDistInMap(u, i_range);
        }
    private:
        WorldObject const* i_obj;
        float i_range;
        ObjectGuid i_guid;
    };

    class GameObjectByGuidInRangeCheck
    {
    public:
        GameObjectByGuidInRangeCheck(WorldObject const* obj, ObjectGuid guid, float range) : i_obj(obj), i_range(range), i_guid(guid) {}
        WorldObject const& GetFocusObject() const { return *i_obj; }
        bool operator()(GameObject* u)
        {
            if (u && i_obj->IsWithinDistInMap(u, i_range) && sServerFacade.isSpawned(u) && u->GetGOInfo() && u->GetObjectGuid() == i_guid)
                return true;

            return false;
        }
    private:
        WorldObject const* i_obj;
        float i_range;
        ObjectGuid i_guid;
    };

};


Unit* PlayerbotAI::GetUnit(ObjectGuid guid)
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return sObjectAccessor.GetUnit(*bot, guid);
}


Creature* PlayerbotAI::GetCreature(ObjectGuid guid)
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return map->GetCreature(guid);
}

GameObject* PlayerbotAI::GetGameObject(ObjectGuid guid)
{
    if (!guid)
        return NULL;

    Map* map = bot->GetMap();
    if (!map)
        return NULL;

    return map->GetGameObject(guid);
}

bool PlayerbotAI::TellMasterNoFacing(string text, PlayerbotSecurityLevel securityLevel)
{
    Player* master = GetMaster();

    if (!master && sPlayerbotAIConfig.randomBotSayWithoutMaster)
    {
        bot->Say(text, 0);
        return true;
    }

    if (!IsTellAllowed(securityLevel))
        return false;

    time_t lastSaid = whispers[text];
    if (!lastSaid || (time(0) - lastSaid) >= sPlayerbotAIConfig.repeatDelay / 1000)
    {
        whispers[text] = time(0);

        ChatMsg type = CHAT_MSG_WHISPER;
        if (currentChat.second - time(0) >= 1)
            type = currentChat.first;

        WorldPacket data;
        ChatHandler::BuildChatPacket(data,
                type == CHAT_MSG_ADDON ? CHAT_MSG_PARTY : type,
                text.c_str(),
                type == CHAT_MSG_ADDON ? LANG_ADDON : LANG_UNIVERSAL,
                CHAT_TAG_NONE, bot->GetObjectGuid(), bot->GetName());
        sServerFacade.SendPacket(master, data);
    }
    return true;
}

bool PlayerbotAI::TellError(string text, PlayerbotSecurityLevel securityLevel)
{
    Player* master = GetMaster();
    if (!IsTellAllowed(securityLevel) || master->GetPlayerbotAI())
        return false;

    PlayerbotMgr* mgr = master->GetPlayerbotMgr();
    if (mgr) mgr->TellError(bot->GetName(), text);
}

bool PlayerbotAI::IsTellAllowed(PlayerbotSecurityLevel securityLevel)
{
    Player* master = GetMaster();
    if (!master || master->IsBeingTeleported())
        return false;

    if (!GetSecurity()->CheckLevelFor(securityLevel, true, master))
        return false;

    if (sPlayerbotAIConfig.whisperDistance && !bot->GetGroup() && sRandomPlayerbotMgr.IsRandomBot(bot) &&
            master->GetSession()->GetSecurity() < SEC_GAMEMASTER &&
            (bot->GetMapId() != master->GetMapId() || bot->GetDistance(master) > sPlayerbotAIConfig.whisperDistance))
        return false;

    return true;
}

bool PlayerbotAI::TellMaster(string text, PlayerbotSecurityLevel securityLevel)
{
    if (!TellMasterNoFacing(text, securityLevel))
        return false;

    if (master && !sServerFacade.isMoving(bot) && !sServerFacade.IsInCombat(bot) && bot->GetMapId() == master->GetMapId() && !bot->IsTaxiFlying())
    {
        if (!sServerFacade.IsInFront(bot, master, sPlayerbotAIConfig.sightDistance, EMOTE_ANGLE_IN_FRONT))
            sServerFacade.SetFacingTo(bot, master);

        bot->HandleEmoteCommand(EMOTE_ONESHOT_TALK);
    }

    return true;
}

bool IsRealAura(Player* bot, Aura const* aura, Unit* unit)
{
    if (!aura)
        return false;

    if (!sServerFacade.IsHostileTo(unit, bot))
        return true;

    uint32 stacks = aura->GetStackAmount();
    if (stacks >= aura->GetSpellProto()->StackAmount)
        return true;

    if (aura->GetCaster() == bot || IsPositiveSpell(aura->GetSpellProto()) || aura->IsAreaAura())
        return true;

    return false;
}

bool PlayerbotAI::HasAura(string name, Unit* unit, bool maxStack)
{
    if (!unit)
        return false;

    wstring wnamepart;
    if (!Utf8toWStr(name, wnamepart))
        return 0;

    wstrToLower(wnamepart);

	for (uint32 auraType = SPELL_AURA_BIND_SIGHT; auraType < TOTAL_AURAS; auraType++)
	{
		Unit::AuraList const& auras = unit->GetAurasByType((AuraType)auraType);
		for (Unit::AuraList::const_iterator i = auras.begin(); i != auras.end(); i++)
		{
			Aura* aura = *i;
			if (!aura)
				continue;

			const string auraName = aura->GetSpellProto()->SpellName[0];
			if (auraName.empty() || auraName.length() != wnamepart.length() || !Utf8FitTo(auraName, wnamepart))
				continue;

			if (IsRealAura(bot, aura, unit))
            {
                uint32 maxStackAmount = aura->GetSpellProto()->StackAmount;
                return maxStack && maxStackAmount ? aura->GetStackAmount() >= maxStackAmount : true;
            }
		}
    }

    return false;
}

bool PlayerbotAI::HasAura(uint32 spellId, const Unit* unit)
{
	if (!spellId || !unit)
		return false;

	for (uint32 effect = EFFECT_INDEX_0; effect <= EFFECT_INDEX_2; effect++)
	{
		Aura* aura = ((Unit*)unit)->GetAura(spellId, (SpellEffectIndex)effect);

		if (IsRealAura(bot, aura, (Unit*)unit))
			return true;
	}

	return false;
}


bool PlayerbotAI::HasAnyAuraOf(Unit* player, ...)
{
    if (!player)
        return false;

    va_list vl;
    va_start(vl, player);

    const char* cur;
    do {
        cur = va_arg(vl, const char*);
        if (cur && HasAura(cur, player)) {
            va_end(vl);
            return true;
        }
    }
    while (cur);

    va_end(vl);
    return false;
}

bool PlayerbotAI::CanCastSpell(string name, Unit* target, uint8 effectMask, Item* itemTarget)
{
    return CanCastSpell(aiObjectContext->GetValue<uint32>("spell id", name)->Get(), target, true, itemTarget);
}

bool PlayerbotAI::CanCastSpell(uint32 spellid, Unit* target, uint8 effectMask, bool checkHasSpell, Item* itemTarget)
{
    if (!spellid)
        return false;

    if (!target)
        target = bot;

    Pet* pet = bot->GetPet();
    if (pet && pet->HasSpell(spellid))
        return true;

    if (checkHasSpell && !bot->HasSpell(spellid))
        return false;

#ifdef MANGOS
    if (bot->HasSpellCooldown(spellid))
        return false;
#endif

	SpellEntry const *spellInfo = sServerFacade.LookupSpellInfo(spellid);
	if (!spellInfo)
        return false;

	if (!itemTarget)
	{
        bool positiveSpell = IsPositiveSpell(spellInfo);
        if (positiveSpell && sServerFacade.IsHostileTo(bot, target))
            return false;

        if (!positiveSpell && sServerFacade.IsFriendlyTo(bot, target))
            return false;

        bool damage = false;
        for (int32 i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; i++)
        {
            if (spellInfo->Effect[(SpellEffectIndex)i] == SPELL_EFFECT_SCHOOL_DAMAGE)
            {
                damage = true;
                break;
            }
        }

#ifdef MANGOS
        if (target->IsImmuneToSpell(spellInfo, false))
#endif
#ifdef CMANGOS
        if (target->IsImmuneToSpell(spellInfo, false, GetSpellSchoolMask(spellInfo)))
#endif
            return false;

        if (!damage)
        {
            for (int32 i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; i++)
            {
                bool immune = target->IsImmuneToSpellEffect(spellInfo, (SpellEffectIndex)i, false);
                if (immune)
                    return false;
            }
        }

        if (bot != target && bot->GetDistance(target) > sPlayerbotAIConfig.sightDistance)
            return false;
	}

	ObjectGuid oldSel = bot->GetSelectionGuid();
	bot->SetSelectionGuid(target->GetObjectGuid());
	Spell *spell = new Spell(bot, spellInfo, false);

    spell->m_targets.setUnitTarget(target);
    spell->m_CastItem = itemTarget ? itemTarget : aiObjectContext->GetValue<Item*>("item for spell", spellid)->Get();
    spell->m_targets.setItemTarget(spell->m_CastItem);

    SpellCastResult result = spell->CheckCast(true);
    delete spell;
	if (oldSel)
		bot->SetSelectionGuid(oldSel);

    switch (result)
    {
    case SPELL_FAILED_NOT_INFRONT:
    case SPELL_FAILED_NOT_STANDING:
    case SPELL_FAILED_UNIT_NOT_INFRONT:
    case SPELL_FAILED_MOVING:
    case SPELL_FAILED_TRY_AGAIN:
    case SPELL_CAST_OK:
        return true;
    default:
        return false;
    }
}

uint8 PlayerbotAI::GetHealthPercent(const Unit& target) const
{
   return (static_cast<float>(target.GetHealth()) / target.GetMaxHealth()) * 100;
}

uint8 PlayerbotAI::GetHealthPercent() const
{
   return GetHealthPercent(*bot);
}

uint8 PlayerbotAI::GetManaPercent(const Unit& target) const
{
   return (static_cast<float>(target.GetPower(POWER_MANA)) / target.GetMaxPower(POWER_MANA)) * 100;
}

uint8 PlayerbotAI::GetManaPercent() const
{
   return GetManaPercent(*bot);
}

bool PlayerbotAI::CastSpell(string name, Unit* target, Item* itemTarget)
{
    bool result = CastSpell(aiObjectContext->GetValue<uint32>("spell id", name)->Get(), target, itemTarget);
    if (result)
    {
        aiObjectContext->GetValue<time_t>("last spell cast time", name)->Set(time(0));
    }

    return result;
}

bool PlayerbotAI::CastSpell(uint32 spellId, Unit* target, Item* itemTarget)
{
    if (!spellId)
        return false;

    if (!target)
        target = bot;

    Pet* pet = bot->GetPet();
	SpellEntry const *pSpellInfo = sServerFacade.LookupSpellInfo(spellId);
	if (pet && pet->HasSpell(spellId))
    {
	    bool autocast = false;
	    for(AutoSpellList::iterator i = pet->m_autospells.begin(); i != pet->m_autospells.end(); ++i)
	    {
	        if (*i == spellId)
	        {
	            autocast = true;
	            break;
	        }
	    }

		pet->ToggleAutocast(spellId, !autocast);
		ostringstream out;
		out << (autocast ? "|cffff0000|Disabling" : "|cFF00ff00|Enabling") << " pet auto-cast for ";
		out << chatHelper.formatSpell(pSpellInfo);
        TellMaster(out);
        return true;
    }

    aiObjectContext->GetValue<LastMovement&>("last movement")->Get().Set(NULL);
    aiObjectContext->GetValue<time_t>("stay time")->Set(0);

    MotionMaster &mm = *bot->GetMotionMaster();

    if (bot->IsFlying() || bot->IsTaxiFlying())
        return false;

	bot->clearUnitState(UNIT_STAT_CHASE);
	bot->clearUnitState(UNIT_STAT_FOLLOW);

	bool failWithDelay = false;
    if (!bot->IsStandState())
    {
        bot->SetStandState(UNIT_STAND_STATE_STAND);
        failWithDelay = true;
    }

	ObjectGuid oldSel = bot->GetSelectionGuid();
	bot->SetSelectionGuid(target->GetObjectGuid());

    WorldObject* faceTo = target;
    if (!sServerFacade.IsInFront(bot, faceTo, sPlayerbotAIConfig.sightDistance, CAST_ANGLE_IN_FRONT) && !bot->IsTaxiFlying())
    {
        if (!sServerFacade.isMoving(bot)) sServerFacade.SetFacingTo(bot, faceTo);
        failWithDelay = true;
    }

    if (failWithDelay)
    {
        SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
        return false;
    }

    Spell *spell = new Spell(bot, pSpellInfo, false);

    SpellCastTargets targets;
    if (pSpellInfo->Targets & TARGET_FLAG_ITEM)
    {
        spell->m_CastItem = itemTarget ? itemTarget : aiObjectContext->GetValue<Item*>("item for spell", spellId)->Get();
        targets.setItemTarget(spell->m_CastItem);

        if (bot->GetTradeData())
        {
            bot->GetTradeData()->SetSpell(spellId);
			delete spell;
            return true;
        }
    }
    else if (pSpellInfo->Targets & TARGET_FLAG_DEST_LOCATION)
    {
        WorldLocation aoe = aiObjectContext->GetValue<WorldLocation>("aoe position")->Get();
        targets.setDestination(aoe.coord_x, aoe.coord_y, aoe.coord_z);
    }
    else if (pSpellInfo->Targets & TARGET_FLAG_SOURCE_LOCATION)
    {
        targets.setDestination(bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
    }
    else
    {
        targets.setUnitTarget(target);
    }

    if (pSpellInfo->Effect[0] == SPELL_EFFECT_OPEN_LOCK ||
        pSpellInfo->Effect[0] == SPELL_EFFECT_SKINNING)
    {
        LootObject loot = *aiObjectContext->GetValue<LootObject>("loot target");
        GameObject* go = GetGameObject(loot.guid);
        if (go && sServerFacade.isSpawned(go))
        {
            WorldPacket packetgouse(CMSG_GAMEOBJ_USE, 8);
            packetgouse << loot.guid;
            bot->GetSession()->HandleGameObjectUseOpcode(packetgouse);
            targets.setGOTarget(go);
            faceTo = go;
        }
        else
        {
            Unit* creature = GetUnit(loot.guid);
            if (creature)
            {
                targets.setUnitTarget(creature);
                faceTo = creature;
            }
        }
    }

#ifdef MANGOS
    spell->prepare(&targets);
#endif
#ifdef CMANGOS
    spell->SpellStart(&targets);
#endif

    if (sServerFacade.isMoving(bot) && spell->GetCastTime())
    {
        bot->StopMoving();
        SetNextCheckDelay(sPlayerbotAIConfig.globalCoolDown);
        spell->cancel();
        //delete spell;
        return false;
    }

    if (pSpellInfo->Effect[0] == SPELL_EFFECT_OPEN_LOCK ||
        pSpellInfo->Effect[0] == SPELL_EFFECT_SKINNING)
    {
        LootObject loot = *aiObjectContext->GetValue<LootObject>("loot target");
        if (!loot.IsLootPossible(bot))
        {
            spell->cancel();
            //delete spell;
            return false;
        }
    }

    if (!urand(0, 50) && sServerFacade.IsInCombat(bot))
    {
        vector<uint32> sounds;
        sounds.push_back(TEXTEMOTE_OPENFIRE);
        sounds.push_back(305);
        sounds.push_back(307);
        PlaySound(sounds[urand(0, sounds.size() - 1)]);
    }

    WaitForSpellCast(spell);
    aiObjectContext->GetValue<LastSpellCast&>("last spell cast")->Get().Set(spellId, target->GetObjectGuid(), time(0));
    aiObjectContext->GetValue<ai::PositionMap&>("position")->Get()["random"].Reset();

    if (oldSel)
        bot->SetSelectionGuid(oldSel);

    return true;
}

void PlayerbotAI::WaitForSpellCast(Spell *spell)
{
    const SpellEntry* const pSpellInfo = spell->m_spellInfo;

    float castTime = spell->GetCastTime();
	if (IsChanneledSpell(pSpellInfo))
    {
        int32 duration = GetSpellDuration(pSpellInfo);
        if (duration > 0)
            castTime += duration;
    }

    castTime = ceil(castTime);

    uint32 globalCooldown = CalculateGlobalCooldown(pSpellInfo->Id);
    if (castTime < globalCooldown)
        castTime = globalCooldown;

    SetNextCheckDelay(castTime + sPlayerbotAIConfig.reactDelay);
}

void PlayerbotAI::InterruptSpell()
{
    for (int type = CURRENT_MELEE_SPELL; type < CURRENT_CHANNELED_SPELL; type++)
    {
        Spell* spell = bot->GetCurrentSpell((CurrentSpellTypes)type);
        if (!spell)
            continue;

        if (IsPositiveSpell(spell->m_spellInfo))
            continue;

        bot->InterruptSpell((CurrentSpellTypes)type);

        WorldPacket data(SMSG_SPELL_FAILURE, 8 + 1 + 4 + 1);
        data.appendPackGUID(bot->GetObjectGuid().GetRawValue());
        data << uint8(1);
        data << uint32(spell->m_spellInfo->Id);
        data << uint8(0);
        sServerFacade.SendMessageToSet(bot, data, true);

        data.Initialize(SMSG_SPELL_FAILED_OTHER, 8 + 1 + 4 + 1);
        data.appendPackGUID(bot->GetObjectGuid().GetRawValue());
        data << uint8(1);
        data << uint32(spell->m_spellInfo->Id);
        data << uint8(0);
        sServerFacade.SendMessageToSet(bot, data, true);

        SpellInterrupted(spell->m_spellInfo->Id);
    }
}


void PlayerbotAI::RemoveAura(string name)
{
    uint32 spellid = aiObjectContext->GetValue<uint32>("spell id", name)->Get();
    if (spellid && HasAura(spellid, bot))
        bot->RemoveAurasDueToSpell(spellid);
}

bool PlayerbotAI::IsInterruptableSpellCasting(Unit* target, string spell, uint8 effectMask)
{
	uint32 spellid = aiObjectContext->GetValue<uint32>("spell id", spell)->Get();
	if (!spellid || !target->IsNonMeleeSpellCasted(true))
		return false;

	SpellEntry const *spellInfo = sServerFacade.LookupSpellInfo(spellid);
	if (!spellInfo)
		return false;

	for (int32 i = EFFECT_INDEX_0; i <= EFFECT_INDEX_2; i++)
	{
		if ((spellInfo->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT) && spellInfo->PreventionType == SPELL_PREVENTION_TYPE_SILENCE)
			return true;

		if ((spellInfo->Effect[i] == SPELL_EFFECT_INTERRUPT_CAST) &&
			!target->IsImmuneToSpellEffect(spellInfo, (SpellEffectIndex)i, true))
			return true;
	}

	return false;
}

bool PlayerbotAI::HasAuraToDispel(Unit* target, uint32 dispelType)
{
	for (uint32 type = SPELL_AURA_NONE; type < TOTAL_AURAS; ++type)
	{
		Unit::AuraList const& auras = target->GetAurasByType((AuraType)type);
		for (Unit::AuraList::const_iterator itr = auras.begin(); itr != auras.end(); ++itr)
		{
			const Aura* aura = *itr;
			const SpellEntry* entry = aura->GetSpellProto();
			uint32 spellId = entry->Id;

			bool isPositiveSpell = IsPositiveSpell(spellId);
			if (isPositiveSpell && sServerFacade.IsFriendlyTo(bot, target))
				continue;

			if (!isPositiveSpell && sServerFacade.IsHostileTo(bot, target))
				continue;

			if (sPlayerbotAIConfig.dispelAuraDuration && aura->GetAuraDuration() && aura->GetAuraDuration() < (int32)sPlayerbotAIConfig.dispelAuraDuration)
			    return false;

			if (canDispel(entry, dispelType))
				return true;
		}
	}
	return false;
}



#ifndef WIN32
inline int strcmpi(const char* s1, const char* s2)
{
    for (; *s1 && *s2 && (toupper(*s1) == toupper(*s2)); ++s1, ++s2);
    return *s1 - *s2;
}
#endif

bool PlayerbotAI::canDispel(const SpellEntry* entry, uint32 dispelType)
{
    if (entry->Dispel != dispelType)
        return false;

    return !entry->SpellName[0] ||
        (strcmpi((const char*)entry->SpellName[0], "demon skin") &&
        strcmpi((const char*)entry->SpellName[0], "mage armor") &&
        strcmpi((const char*)entry->SpellName[0], "frost armor") &&
        strcmpi((const char*)entry->SpellName[0], "wavering will") &&
        strcmpi((const char*)entry->SpellName[0], "chilled") &&
        strcmpi((const char*)entry->SpellName[0], "ice armor"));
}

bool IsAlliance(uint8 race)
{
    return race == RACE_HUMAN || race == RACE_DWARF || race == RACE_NIGHTELF ||
#ifndef MANGOSBOT_ZERO
        race == RACE_DRAENEI ||
#endif
            race == RACE_GNOME;
}

bool PlayerbotAI::IsOpposing(Player* player)
{
    return IsOpposing(player->getRace(), bot->getRace());
}

bool PlayerbotAI::IsOpposing(uint8 race1, uint8 race2)
{
    return (IsAlliance(race1) && !IsAlliance(race2)) || (!IsAlliance(race1) && IsAlliance(race2));
}

void PlayerbotAI::RemoveShapeshift()
{
    RemoveAura("bear form");
    RemoveAura("dire bear form");
    RemoveAura("moonkin form");
    RemoveAura("travel form");
    RemoveAura("cat form");
    RemoveAura("flight form");
    RemoveAura("swift flight form");
    RemoveAura("aquatic form");
    RemoveAura("ghost wolf");
    RemoveAura("tree of life");
}

uint32 PlayerbotAI::GetEquipGearScore(Player* player, bool withBags, bool withBank)
{
    std::vector<uint32> gearScore(EQUIPMENT_SLOT_END);
    uint32 twoHandScore = 0;

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
        if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            _fillGearScoreData(player, item, &gearScore, twoHandScore);
    }

    if (withBags)
    {
        // check inventory
        for (int i = INVENTORY_SLOT_ITEM_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                _fillGearScoreData(player, item, &gearScore, twoHandScore);
        }

        // check bags
        for (int i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; ++i)
        {
            if (Bag* pBag = (Bag*)player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                for (uint32 j = 0; j < pBag->GetBagSize(); ++j)
                {
                    if (Item* item2 = pBag->GetItemByPos(j))
                        _fillGearScoreData(player, item2, &gearScore, twoHandScore);
                }
            }
        }
    }

    if (withBank)
    {
        for (uint8 i = BANK_SLOT_ITEM_START; i < BANK_SLOT_ITEM_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                _fillGearScoreData(player, item, &gearScore, twoHandScore);
        }

        for (uint8 i = BANK_SLOT_BAG_START; i < BANK_SLOT_BAG_END; ++i)
        {
            if (Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            {
                if (item->IsBag())
                {
                    Bag* bag = (Bag*)item;
                    for (uint8 j = 0; j < bag->GetBagSize(); ++j)
                    {
                        if (Item* item2 = bag->GetItemByPos(j))
                            _fillGearScoreData(player, item2, &gearScore, twoHandScore);
                    }
                }
            }
        }
    }

    uint8 count = EQUIPMENT_SLOT_END - 2;   // ignore body and tabard slots
    uint32 sum = 0;

    // check if 2h hand is higher level than main hand + off hand
    if (gearScore[EQUIPMENT_SLOT_MAINHAND] + gearScore[EQUIPMENT_SLOT_OFFHAND] < twoHandScore * 2)
    {
        gearScore[EQUIPMENT_SLOT_OFFHAND] = 0;  // off hand is ignored in calculations if 2h weapon has higher score
        --count;
        gearScore[EQUIPMENT_SLOT_MAINHAND] = twoHandScore;
    }

    for (uint8 i = EQUIPMENT_SLOT_START; i < EQUIPMENT_SLOT_END; ++i)
    {
       sum += gearScore[i];
    }

    if (count)
    {
        uint32 res = uint32(sum / count);
        return res;
    }
    else
        return 0;
}

void PlayerbotAI::_fillGearScoreData(Player *player, Item* item, std::vector<uint32>* gearScore, uint32& twoHandScore)
{
    if (!item)
        return;

    if (player->CanUseItem(item->GetProto()) != EQUIP_ERR_OK)
        return;

    uint8 type   = item->GetProto()->InventoryType;
    uint32 level = item->GetProto()->ItemLevel;

    switch (type)
    {
        case INVTYPE_2HWEAPON:
            twoHandScore = std::max(twoHandScore, level);
            break;
        case INVTYPE_WEAPON:
        case INVTYPE_WEAPONMAINHAND:
            (*gearScore)[SLOT_MAIN_HAND] = std::max((*gearScore)[SLOT_MAIN_HAND], level);
            break;
        case INVTYPE_SHIELD:
        case INVTYPE_WEAPONOFFHAND:
            (*gearScore)[EQUIPMENT_SLOT_OFFHAND] = std::max((*gearScore)[EQUIPMENT_SLOT_OFFHAND], level);
            break;
        case INVTYPE_THROWN:
        case INVTYPE_RANGEDRIGHT:
        case INVTYPE_RANGED:
        case INVTYPE_QUIVER:
        case INVTYPE_RELIC:
            (*gearScore)[EQUIPMENT_SLOT_RANGED] = std::max((*gearScore)[EQUIPMENT_SLOT_RANGED], level);
            break;
        case INVTYPE_HEAD:
            (*gearScore)[EQUIPMENT_SLOT_HEAD] = std::max((*gearScore)[EQUIPMENT_SLOT_HEAD], level);
            break;
        case INVTYPE_NECK:
            (*gearScore)[EQUIPMENT_SLOT_NECK] = std::max((*gearScore)[EQUIPMENT_SLOT_NECK], level);
            break;
        case INVTYPE_SHOULDERS:
            (*gearScore)[EQUIPMENT_SLOT_SHOULDERS] = std::max((*gearScore)[EQUIPMENT_SLOT_SHOULDERS], level);
            break;
        case INVTYPE_BODY:
            (*gearScore)[EQUIPMENT_SLOT_BODY] = std::max((*gearScore)[EQUIPMENT_SLOT_BODY], level);
            break;
        case INVTYPE_CHEST:
            (*gearScore)[EQUIPMENT_SLOT_CHEST] = std::max((*gearScore)[EQUIPMENT_SLOT_CHEST], level);
            break;
        case INVTYPE_WAIST:
            (*gearScore)[EQUIPMENT_SLOT_WAIST] = std::max((*gearScore)[EQUIPMENT_SLOT_WAIST], level);
            break;
        case INVTYPE_LEGS:
            (*gearScore)[EQUIPMENT_SLOT_LEGS] = std::max((*gearScore)[EQUIPMENT_SLOT_LEGS], level);
            break;
        case INVTYPE_FEET:
            (*gearScore)[EQUIPMENT_SLOT_FEET] = std::max((*gearScore)[EQUIPMENT_SLOT_FEET], level);
            break;
        case INVTYPE_WRISTS:
            (*gearScore)[EQUIPMENT_SLOT_WRISTS] = std::max((*gearScore)[EQUIPMENT_SLOT_WRISTS], level);
            break;
        case INVTYPE_HANDS:
            (*gearScore)[EQUIPMENT_SLOT_HEAD] = std::max((*gearScore)[EQUIPMENT_SLOT_HEAD], level);
            break;
        // equipped gear score check uses both rings and trinkets for calculation, assume that for bags/banks it is the same
        // with keeping second highest score at second slot
        case INVTYPE_FINGER:
        {
            if ((*gearScore)[EQUIPMENT_SLOT_FINGER1] < level)
            {
                (*gearScore)[EQUIPMENT_SLOT_FINGER2] = (*gearScore)[EQUIPMENT_SLOT_FINGER1];
                (*gearScore)[EQUIPMENT_SLOT_FINGER1] = level;
            }
            else if ((*gearScore)[EQUIPMENT_SLOT_FINGER2] < level)
                (*gearScore)[EQUIPMENT_SLOT_FINGER2] = level;
            break;
        }
        case INVTYPE_TRINKET:
        {
            if ((*gearScore)[EQUIPMENT_SLOT_TRINKET1] < level)
            {
                (*gearScore)[EQUIPMENT_SLOT_TRINKET2] = (*gearScore)[EQUIPMENT_SLOT_TRINKET1];
                (*gearScore)[EQUIPMENT_SLOT_TRINKET1] = level;
            }
            else if ((*gearScore)[EQUIPMENT_SLOT_TRINKET2] < level)
                (*gearScore)[EQUIPMENT_SLOT_TRINKET2] = level;
            break;
        }
        case INVTYPE_CLOAK:
            (*gearScore)[EQUIPMENT_SLOT_BACK] = std::max((*gearScore)[EQUIPMENT_SLOT_BACK], level);
            break;
        default:
            break;
    }
}

string PlayerbotAI::HandleRemoteCommand(string command)
{
    if (command == "state")
    {
        switch (currentState)
        {
        case BOT_STATE_COMBAT:
            return "combat";
        case BOT_STATE_DEAD:
            return "dead";
        case BOT_STATE_NON_COMBAT:
            return "non-combat";
        default:
            return "unknown";
        }
    }
    else if (command == "position")
    {
        ostringstream out; out << bot->GetPositionX() << " " << bot->GetPositionY() << " " << bot->GetPositionZ() << " " << bot->GetMapId() << " " << bot->GetOrientation();
        uint32 area = bot->GetAreaId();
        if (const AreaTableEntry* areaEntry = GetAreaEntryByAreaID(area))
        {
            if (AreaTableEntry const* zoneEntry = areaEntry->zone ? GetAreaEntryByAreaID(areaEntry->zone) : areaEntry)
                out << " |" << zoneEntry->area_name[0] << "|";
        }
        return out.str();
    }
    else if (command == "tpos")
    {
        Unit* target = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (!target) {
            return "";
        }

        ostringstream out; out << target->GetPositionX() << " " << target->GetPositionY() << " " << target->GetPositionZ() << " " << target->GetMapId() << " " << target->GetOrientation();
        return out.str();
    }
    else if (command == "movement")
    {
        LastMovement& data = *GetAiObjectContext()->GetValue<LastMovement&>("last movement");
        ostringstream out; out << data.lastMoveToX << " " << data.lastMoveToY << " " << data.lastMoveToZ << " " << bot->GetMapId() << " " << data.lastMoveToOri;
        return out.str();
    }
    else if (command == "target")
    {
        Unit* target = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (!target) {
            return "";
        }

        return target->GetName();
    }
    else if (command == "hp")
    {
        int pct = (int)((static_cast<float> (bot->GetHealth()) / bot->GetMaxHealth()) * 100);
        ostringstream out; out << pct << "%";

        Unit* target = *GetAiObjectContext()->GetValue<Unit*>("current target");
        if (!target) {
            return out.str();
        }

        pct = (int)((static_cast<float> (target->GetHealth()) / target->GetMaxHealth()) * 100);
        out << " / " << pct << "%";
        return out.str();
    }
    else if (command == "strategy")
    {
        return currentEngine->ListStrategies();
    }
    else if (command == "action")
    {
        return currentEngine->GetLastAction();
    }
    else if (command == "values")
    {
        return GetAiObjectContext()->FormatValues();
    }
    ostringstream out; out << "invalid command: " << command;
    return out.str();
}

bool PlayerbotAI::HasSkill(SkillType skill)
{
    return bot->HasSkill(skill) && bot->GetSkillValue(skill) > 0;
}

bool ChatHandler::HandlePlayerbotCommand(char* args)
{
    return PlayerbotMgr::HandlePlayerbotMgrCommand(this, args);
}

bool ChatHandler::HandleRandomPlayerbotCommand(char* args)
{
    return RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(this, args);
}

bool ChatHandler::HandleAhBotCommand(char* args)
{
    return ahbot::AhBot::HandleAhBotCommand(this, args);
}

bool ChatHandler::HandleGuildTaskCommand(char* args)
{
    return GuildTaskMgr::HandleConsoleCommand(this, args);
}

float PlayerbotAI::GetRange(string type)
{
    float val = 0;
    if (aiObjectContext) val = aiObjectContext->GetValue<float>("range", type)->Get();
    if (abs(val) >= 0.1f) return val;

    if (type == "spell") return sPlayerbotAIConfig.spellDistance;
    if (type == "shoot") return sPlayerbotAIConfig.shootDistance;
    if (type == "flee") return sPlayerbotAIConfig.fleeDistance;
    if (type == "heal") return sPlayerbotAIConfig.healDistance;
    return 0;
}

void PlayerbotAI::Ping(float x, float y)
{
    WorldPacket data(MSG_MINIMAP_PING, (8 + 4 + 4));
    data << bot->GetObjectGuid();
    data << x;
    data << y;
    bot->GetGroup()->BroadcastPacket(
#ifdef MANGOS
        &data,
#endif
#ifdef CMANGOS
        data,
#endif
        true, -1, bot->GetObjectGuid());
}

//Find Poison ...Natsukawa
Item* PlayerbotAI::FindPoison() const
{
   // list out items in main backpack
   for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
   {
      Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
      if (pItem)
      {
         const ItemPrototype* const pItemProto = pItem->GetProto();

         if (!pItemProto || bot->CanUseItem(pItemProto) != EQUIP_ERR_OK)
            continue;

         if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == 6)
            return pItem;
      }
   }
   // list out items in other removable backpacks
   for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
   {
      const Bag* const pBag = (Bag *)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
      if (pBag)
         for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
         {
            Item* const pItem = bot->GetItemByPos(bag, slot);
            if (pItem)
            {
               const ItemPrototype* const pItemProto = pItem->GetProto();

               if (!pItemProto || bot->CanUseItem(pItemProto) != EQUIP_ERR_OK)
                  continue;

               if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == 6)
                  return pItem;
            }
         }
   }
   return NULL;
}

Item* PlayerbotAI::FindConsumable(uint32 displayId) const
{
   // list out items in main backpack
   for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
   {
      Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
      if (pItem)
      {
         const ItemPrototype* const pItemProto = pItem->GetProto();

         if (!pItemProto || bot->CanUseItem(pItemProto) != EQUIP_ERR_OK)
            continue;

         if ((pItemProto->Class == ITEM_CLASS_CONSUMABLE || pItemProto->Class == ITEM_CLASS_TRADE_GOODS) && pItemProto->DisplayInfoID == displayId)
            return pItem;
      }
   }
   // list out items in other removable backpacks
   for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
   {
      const Bag* const pBag = (Bag *)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
      if (pBag)
         for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
         {
            Item* const pItem = bot->GetItemByPos(bag, slot);
            if (pItem)
            {
               const ItemPrototype* const pItemProto = pItem->GetProto();

               if (!pItemProto || bot->CanUseItem(pItemProto) != EQUIP_ERR_OK)
                  continue;

               if ((pItemProto->Class == ITEM_CLASS_CONSUMABLE || pItemProto->Class == ITEM_CLASS_TRADE_GOODS) && pItemProto->DisplayInfoID == displayId)
                  return pItem;
            }
         }
   }
   return NULL;
}

Item* PlayerbotAI::FindBandage() const
{
   // list out items in main backpack
   for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
   {
      Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
      if (pItem)
      {
         const ItemPrototype* const pItemProto = pItem->GetProto();

         if (!pItemProto || bot->CanUseItem(pItemProto) != EQUIP_ERR_OK)
            continue;
#ifdef MANGOSBOT_ZERO
         if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == ITEM_SUBCLASS_FOOD)
#else
         if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == ITEM_SUBCLASS_BANDAGE)
#endif
            return pItem;
      }
   }
   // list out items in other removable backpacks
   for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
   {
      const Bag* const pBag = (Bag *)bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
      if (pBag)
         for (uint8 slot = 0; slot < pBag->GetBagSize(); ++slot)
         {
            Item* const pItem = bot->GetItemByPos(bag, slot);
            if (pItem)
            {
               const ItemPrototype* const pItemProto = pItem->GetProto();

               if (!pItemProto || bot->CanUseItem(pItemProto) != EQUIP_ERR_OK)
                  continue;

#ifdef MANGOSBOT_ZERO
               if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == ITEM_SUBCLASS_FOOD)
#else
               if (pItemProto->Class == ITEM_CLASS_CONSUMABLE && pItemProto->SubClass == ITEM_SUBCLASS_BANDAGE)
#endif
                  return pItem;
            }
         }
   }
   return nullptr;
}

static const uint32 uPriorizedSharpStoneIds[8] =
{
    ADAMANTITE_SHARPENING_DISPLAYID, FEL_SHARPENING_DISPLAYID, ELEMENTAL_SHARPENING_DISPLAYID, DENSE_SHARPENING_DISPLAYID,
    SOLID_SHARPENING_DISPLAYID, HEAVY_SHARPENING_DISPLAYID, COARSE_SHARPENING_DISPLAYID, ROUGH_SHARPENING_DISPLAYID
};

static const uint32 uPriorizedWeightStoneIds[7] =
{
    ADAMANTITE_WEIGHTSTONE_DISPLAYID, FEL_WEIGHTSTONE_DISPLAYID, DENSE_WEIGHTSTONE_DISPLAYID, SOLID_WEIGHTSTONE_DISPLAYID,
    HEAVY_WEIGHTSTONE_DISPLAYID, COARSE_WEIGHTSTONE_DISPLAYID, ROUGH_WEIGHTSTONE_DISPLAYID
};

/**
 * FindStoneFor()
 * return Item* Returns sharpening/weight stone item eligible to enchant a bot weapon
 *
 * params:weapon Item* the weap�n the function should search and return a enchanting item for
 * return nullptr if no relevant item is found in bot inventory, else return a sharpening or weight
 * stone based on the weapon subclass
 *
 */
Item* PlayerbotAI::FindStoneFor(Item* weapon) const
{
   Item* stone;
   ItemPrototype const* pProto = weapon->GetProto();
   if (pProto && (pProto->SubClass == ITEM_SUBCLASS_WEAPON_SWORD || pProto->SubClass == ITEM_SUBCLASS_WEAPON_SWORD2
      || pProto->SubClass == ITEM_SUBCLASS_WEAPON_AXE || pProto->SubClass == ITEM_SUBCLASS_WEAPON_AXE2
      || pProto->SubClass == ITEM_SUBCLASS_WEAPON_DAGGER))
   {
      for (uint8 i = 0; i < countof(uPriorizedSharpStoneIds); ++i)
      {
         stone = FindConsumable(uPriorizedSharpStoneIds[i]);
         if (stone)
            return stone;
      }
   }
   else if (pProto && (pProto->SubClass == ITEM_SUBCLASS_WEAPON_MACE || pProto->SubClass == ITEM_SUBCLASS_WEAPON_MACE2))
   {
      for (uint8 i = 0; i < countof(uPriorizedWeightStoneIds); ++i)
      {
         stone = FindConsumable(uPriorizedWeightStoneIds[i]);
         if (stone)
            return stone;
      }
   }

   return nullptr;
}

static const uint32 uPriorizedWizardOilIds[5] =
{
    MINOR_WIZARD_OIL, LESSER_WIZARD_OIL, BRILLIANT_WIZARD_OIL, WIZARD_OIL, SUPERIOR_WIZARD_OIL
};

static const uint32 uPriorizedManaOilIds[4] =
{
   MINOR_MANA_OIL, LESSER_MANA_OIL, BRILLIANT_MANA_OIL, SUPERIOR_MANA_OIL,
};

Item* PlayerbotAI::FindOilFor(Item* weapon) const
{
   Item* oil;
   ItemPrototype const* pProto = weapon->GetProto();
   if (pProto && (pProto->SubClass == ITEM_SUBCLASS_WEAPON_SWORD || pProto->SubClass == ITEM_SUBCLASS_WEAPON_STAFF || pProto->SubClass == ITEM_SUBCLASS_WEAPON_DAGGER))
   {
      for (uint8 i = 0; i < countof(uPriorizedWizardOilIds); ++i)
      {
         oil = FindConsumable(uPriorizedWizardOilIds[i]);
         if (!oil)
            oil = FindConsumable(uPriorizedManaOilIds[i]);
         if (oil)
            return oil;
      }
   }
   else if (pProto && (pProto->SubClass == ITEM_SUBCLASS_WEAPON_MACE || pProto->SubClass == ITEM_SUBCLASS_WEAPON_MACE2))
   {
      for (uint8 i = 0; i < countof(uPriorizedManaOilIds); ++i)
      {
         oil = FindConsumable(uPriorizedManaOilIds[i]);
         if (!oil)
            oil = FindConsumable(uPriorizedWizardOilIds[i]);
         if (oil)
            return oil;
      }
   }

   return nullptr;
}

//  on self
void PlayerbotAI::ImbueItem(Item* item)
{
   ImbueItem(item, TARGET_FLAG_SELF, ObjectGuid());
}

//  item on unit
void PlayerbotAI::ImbueItem(Item* item, Unit* target)
{
   if (!target)
      return;

   ImbueItem(item, TARGET_FLAG_UNIT, target->GetObjectGuid());
}

//  item on equipped item
void PlayerbotAI::ImbueItem(Item* item, uint8 targetInventorySlot)
{
   if (targetInventorySlot >= EQUIPMENT_SLOT_END)
      return;

   Item* const targetItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, targetInventorySlot);
   if (!targetItem)
      return;

   ImbueItem(item, TARGET_FLAG_ITEM, targetItem->GetObjectGuid());
}

// generic item use method
#ifdef MANGOSBOT_ZERO
void PlayerbotAI::ImbueItem(Item* item, uint16 targetFlag, ObjectGuid targetGUID)
#else
void PlayerbotAI::ImbueItem(Item* item, uint32 targetFlag, ObjectGuid targetGUID)
#endif
{
   if (!item)
      return;

   uint8 bagIndex = item->GetBagSlot();
   uint8 slot = item->GetSlot();
   uint8 cast_count = 0;
   ObjectGuid item_guid = item->GetObjectGuid();

   uint32 spellId = 0;
   uint8 spell_index = 0;
   for (uint8 i = 0; i < MAX_ITEM_PROTO_SPELLS; ++i)
   {
      if (item->GetProto()->Spells[i].SpellId > 0)
      {
         spellId = item->GetProto()->Spells[i].SpellId;
         spell_index = i;
         break;
      }
   }

#ifdef CMANGOS
   std::unique_ptr<WorldPacket> packet(new WorldPacket(CMSG_USE_ITEM, 20));
#endif
#ifdef MANGOS
   WorldPacket* packet = new WorldPacket(CMSG_USE_ITEM);
#endif

   *packet << bagIndex;
   *packet << slot;
   *packet << spell_index;
#ifdef MANGOSBOT_ZERO
   *packet << targetFlag;
#endif
#ifdef MANGOSBOT_ONE
   *packet << cast_count;
   *packet << item_guid;
   *packet << targetFlag;
#endif

#ifdef CMANGOS
   if (targetFlag & (TARGET_FLAG_UNIT | TARGET_FLAG_ITEM | TARGET_FLAG_GAMEOBJECT))
#endif
#ifdef MANGOS
   if (targetFlag & (TARGET_FLAG_UNIT | TARGET_FLAG_ITEM | TARGET_FLAG_OBJECT))
#endif
      *packet << targetGUID.WriteAsPacked();

#ifdef CMANGOS
   bot->GetSession()->QueuePacket(std::move(packet));
#endif
#ifdef MANGOS
   bot->GetSession()->QueuePacket(packet);
#endif
}

void PlayerbotAI::EnchantItemT(uint32 spellid, uint8 slot)
{
   Item* pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
   if (!pItem)
    return;
#ifdef MANGOS
   SpellEntry const* spellInfo = sSpellStore.LookupEntry(spellid);
#else
   SpellEntry const* spellInfo = sSpellTemplate.LookupEntry<SpellEntry>(spellid);
#endif
   if (!spellInfo)
      return;

   uint32 enchantid = spellInfo->EffectMiscValue[0];
   if (!enchantid)
   {
      sLog.outError("%s: Invalid enchantid " , enchantid , " report to devs", bot->GetName());
      return;
   }

   if (!((1 << pItem->GetProto()->SubClass) & spellInfo->EquippedItemSubClassMask) &&
      !((1 << pItem->GetProto()->InventoryType) & spellInfo->EquippedItemInventoryTypeMask))
   {
      
      sLog.outError("%s: items could not be enchanted, wrong item type equipped", bot->GetName());

      return;
   }

   bot->ApplyEnchantment(pItem, PERM_ENCHANTMENT_SLOT, false);
   pItem->SetEnchantment(PERM_ENCHANTMENT_SLOT, enchantid, 0, 0);
   bot->ApplyEnchantment(pItem, PERM_ENCHANTMENT_SLOT, true);

   sLog.outDetail("%s: items was enchanted successfully!", bot->GetName());
}

int PlayerbotAI::GetBattleMasterEntryByRace(uint8 race)
{
    switch (race)
    {
    case RACE_HUMAN:       return 14981;
    case RACE_ORC:         return 3890;
    case RACE_DWARF:       return 14982;
    case RACE_NIGHTELF:    return 2302;
    case RACE_UNDEAD:      return 2804;
    case RACE_TAUREN:      return 10360;
    case RACE_GNOME:       return 14982;
    case RACE_TROLL:       return 3890;
#ifndef MANGOSBOT_ZERO
    case RACE_DRAENEI:     return 20118;
    case RACE_BLOODELF:    return 16696;
#endif
    default:    return 0;
    }
}

uint32 PlayerbotAI::GetBattleMasterGuidByRace(uint8 race)
{
    uint32 guid = 0;
    int entry = GetBattleMasterEntryByRace(race);
    if (entry)
        guid = GetCreatureGuidByEntry(entry);
    return guid;
}

const CreatureData* PlayerbotAI::GetCreatureDataByEntry(uint32 entry)
{
    CreatureData const* data = NULL;
    if (entry != 0 && ObjectMgr::GetCreatureTemplate(entry))
    {
        FindCreatureData worker(entry, NULL);
        sObjectMgr.DoCreatureData(worker);
        CreatureDataPair const* dataPair = worker.GetResult();
        if (dataPair)
        {
            data = &dataPair->second;
        }
    }
    return data;
}

uint32 PlayerbotAI::GetCreatureGuidByEntry(uint32 entry)
{
    uint32 guid = 0;

    QueryResult* result;
    result = WorldDatabase.PQuery("SELECT `guid` FROM `creature` WHERE `id` = '%u' LIMIT %u",
        entry, 1);

    if (result)
    {
        do
        {
            Field* fields = result->Fetch();
            guid = fields[0].GetUInt32();

        } while (result->NextRow());

        delete result;
    }
    return guid;
}

uint32 PlayerbotAI::GetBuffedCount(Player* player, string spellname)
{
    Group* group = player->GetGroup();
    if (group)
    {
        uint32 bcount = 0;
        for (GroupReference *gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            Player* member = gref->getSource();
            PlayerbotAI* ai = bot->GetPlayerbotAI();
            if (ai->HasAura(spellname, member, true))
                bcount++;
        }
        return bcount;
    }
    return 0;
}

void PlayerbotAI::UpdateWaypointMovement()
{
    // We already have a path.
    if (m_currentPath)
        return;

    if (bot->isMoving())
        return;

    if (!bot->IsStopped())
        return;

    if (bot->IsInCombat())
        return;

    if (bot->hasUnitState(UNIT_STAT_CAN_NOT_MOVE))
        return;

    switch (bot->GetMotionMaster()->GetCurrentMovementGeneratorType())
    {
    case IDLE_MOTION_TYPE:
    case CHASE_MOTION_TYPE:
    case POINT_MOTION_TYPE:
        break;
    default:
        return;
    }

    if (BattleGround* bg = bot->GetBattleGround())
        if (bg->GetStatus() == STATUS_WAIT_JOIN)
            return;

    if (StartNewPathToObjective())
        return;

    if (StartNewPathFromBeginning())
        return;

    StartNewPathFromAnywhere();
}

void PlayerbotAI::StopMoving()
{
    bot->StopMoving();
    bot->GetMotionMaster()->Clear();
    bot->GetMotionMaster()->MoveIdle();
}
