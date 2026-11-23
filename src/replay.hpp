// Timmy Tracker replays: records your clicks on a Timmylist level and plays the run back in the game when you ask.
// A run is saved when you beat a list level (normal mode only). Only your latest completion of each level is kept.
#pragma once
#include "tl.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace tl::rp {
	struct Ev {
		unsigned int step = 0;
		bool down = false;
		int button = 1;
		bool p1 = true;
	};

	inline std::vector<Ev> g_rec;          // clicks of the current attempt
	inline bool g_recOn = false;           // recording this attempt
	inline std::vector<Ev> g_play;         // the run being played back
	inline size_t g_idx = 0;               // next click to play
	inline bool g_pending = false;         // the next PlayLayer is a replay
	inline bool g_playing = false;         // the open PlayLayer is a replay
	inline bool g_inject = false;          // our own click is going through
	inline int g_playLevel = 0;

	inline std::filesystem::path dir() {
		return Mod::get()->getSaveDir() / "replays";
	}

	inline std::filesystem::path fileFor(int id) {
		return dir() / (std::to_string(id) + ".tlr");
	}

	inline bool exists(int id) {
		std::error_code ec;
		return id > 0 && std::filesystem::exists(fileFor(id), ec);
	}

	inline bool save(int id, unsigned int steps) {
		if (id <= 0 || g_rec.empty()) return false;
		std::error_code ec;
		std::filesystem::create_directories(dir(), ec);
		std::ofstream out(fileFor(id), std::ios::trunc);
		if (!out) return false;
		out << "TLR1 " << id << " " << steps << " " << g_rec.size() << "\n";
		for (auto const& e : g_rec) {
			out << e.step << " " << (e.down ? 1 : 0) << " " << e.button << " " << (e.p1 ? 1 : 0) << "\n";
		}
		return static_cast<bool>(out);
	}

	inline bool load(int id) {
		std::ifstream in(fileFor(id));
		if (!in) return false;
		std::string magic;
		int lv = 0;
		unsigned int steps = 0;
		size_t n = 0;
		if (!(in >> magic >> lv >> steps >> n)) return false;
		if (magic != "TLR1" || lv != id || n == 0 || n > 200000) return false;
		std::vector<Ev> ev;
		ev.reserve(n);
		for (size_t i = 0; i < n; ++i) {
			Ev e;
			int d = 0, p = 1;
			if (!(in >> e.step >> d >> e.button >> p)) return false;
			e.down = d != 0;
			e.p1 = p != 0;
			ev.push_back(e);
		}
		g_play = std::move(ev);
		g_idx = 0;
		g_playLevel = id;
		return true;
	}

	// start watching the saved run of a level (from the level page or the pop-up)
	inline bool watch(int id) {
		auto* lm = GameLevelManager::get();
		GJGameLevel* level = lm ? lm->getSavedLevel(id) : nullptr;
		if (!level) {
			note("Open the level once so the game has it, then try again.", NotificationIcon::Warning, true);
			return false;
		}
		if (!load(id)) {
			note("No replay saved for this level yet. Beat it to record one.", NotificationIcon::Warning, true);
			return false;
		}
		g_pending = true;
		CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, PlayLayer::scene(level, false, false)));
		return true;
	}

	// the PlayLayer just opened: is it a replay?
	inline void onOpen(PlayLayer* pl) {
		g_playing = g_pending;
		g_pending = false;
		if (g_playing) {
			g_rec.clear();
			g_recOn = false;
		}
		if (!g_playing || !pl || !pl->m_uiLayer) return;
		auto win = CCDirector::get()->getWinSize();
		auto* lbl = CCLabelBMFont::create("Timmy Tracker replay", "goldFont.fnt");
		lbl->setScale(0.4f);
		lbl->setOpacity(190);
		lbl->setID("tl-replay-label"_spr);
		lbl->setPosition({ win.width / 2, win.height - 12.f });
		pl->m_uiLayer->addChild(lbl, 60);
	}

	// every restart of the level: start recording a fresh attempt (normal mode only), or rewind the replay
	inline void onReset(PlayLayer* pl) {
		g_rec.clear();
		g_idx = 0;
		g_recOn = !g_playing && !g_pending && opt("record-replays") && counts(pl);
	}

	inline void onQuit() {
		g_playing = false;
		g_pending = false;
		g_recOn = false;
		g_rec.clear();
	}

	// a click happened (called from the handleButton hook)
	inline void record(GJBaseGameLayer* gl, bool down, int button, bool p1) {
		if (!g_recOn) return;
		auto* pl = PlayLayer::get();
		if (!pl || static_cast<GJBaseGameLayer*>(pl) != gl) return;
		if (g_rec.size() >= 200000) return;
		Ev e;
		e.step = gl->m_gameState.m_currentProgress;
		e.down = down;
		e.button = button;
		e.p1 = p1;
		g_rec.push_back(e);
	}

	// the level was completed
	inline void onComplete(PlayLayer* pl) {
		if (g_playing) {
			note("Replay finished", NotificationIcon::Info, true);
			return;
		}
		if (!g_recOn || !counts(pl) || !g_haveIds) return;
		int lv = pl->m_level->m_levelID.value();
		auto* info = infoFor(lv);
		if (!info) return;
		if (save(lv, pl->m_gameState.m_currentProgress)) {
			note("Replay saved for " + info->name + ". Watch it from the level page or the Timmy Tracker list.", NotificationIcon::Success);
		}
		g_recOn = false;
	}
}
