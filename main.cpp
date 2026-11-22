// Timmylist Live: sends your best % and completions on The Timmylist's levels to your Timmylist account.
// Everything runs on the main thread. Only levels that are on the list are sent; practice and testmode are ignored.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

namespace tl {
	struct Ev {
		int lv = 0;
		int p = 0;
		bool c = false;
	};

	inline std::unordered_set<int> g_ids;            // levels that are on the Timmylist
	inline bool g_haveIds = false;
	inline bool g_fetching = false;
	inline std::vector<std::function<void()>> g_waiting;
	inline std::unordered_map<int, int> g_best;      // best % already known for a level this session
	inline std::vector<Ev> g_queue;                  // events the server hasn't confirmed yet
	inline bool g_sending = false;
	inline bool g_greeted = false;
	inline bool g_warned = false;
	inline int g_nowLv = 0;
	inline int g_nowP = 0;

	inline std::string serverUrl() {
		std::string s = Mod::get()->getSettingValue<std::string>("server");
		while (!s.empty() && (s.back() == '/' || s.back() == ' ')) s.pop_back();
		if (s.empty()) s = "https://www.timmylist.org";
		return s + "/api/social";
	}

	inline std::string linkCode() {
		std::string raw = Mod::get()->getSettingValue<std::string>("link-code");
		std::string out;
		for (char ch : raw) {
			if ((ch >= 'a' && ch <= 'z')) ch = static_cast<char>(ch - 'a' + 'A');
			if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) out.push_back(ch);
		}
		return out;
	}

	inline std::string jsonEsc(std::string const& s) {
		std::string o;
		for (char ch : s) {
			if (ch == '"' || ch == '\\') { o.push_back('\\'); o.push_back(ch); }
			else if (static_cast<unsigned char>(ch) < 0x20) o.push_back(' ');
			else o.push_back(ch);
		}
		return o;
	}

	inline void note(std::string const& text, NotificationIcon icon) {
		Notification::create(text, icon, 3.f)->show();
	}

	inline std::string gdName() {
		auto* acm = GJAccountManager::sharedState();
		if (!acm) return "";
		return std::string(acm->m_username);
	}

	inline void fetchIds(std::function<void()> then) {
		if (g_haveIds) { then(); return; }
		g_waiting.push_back(std::move(then));
		if (g_fetching) return;
		g_fetching = true;
		auto req = web::WebRequest();
		req.header("Content-Type", "application/json");
		req.userAgent("TimmylistLive/1.0");
		req.bodyString("{\"a\":\"gdLevels\"}");
		async::spawn(req.post(serverUrl()), [](web::WebResponse res) {
			g_fetching = false;
			if (res.ok()) {
				std::string body = res.string().unwrapOr("");
				auto at = body.find("\"ids\"");
				if (at != std::string::npos) {
					auto open = body.find('[', at);
					auto close = body.find(']', at);
					if (open != std::string::npos && close != std::string::npos && close > open) {
						std::unordered_set<int> ids;
						long cur = -1;
						for (size_t i = open + 1; i < close; ++i) {
							char ch = body[i];
							if (ch >= '0' && ch <= '9') cur = (cur < 0 ? 0 : cur) * 10 + (ch - '0');
							else if (cur >= 0) { if (cur > 0 && cur < 2147483647L) ids.insert(static_cast<int>(cur)); cur = -1; }
						}
						if (cur > 0 && cur < 2147483647L) ids.insert(static_cast<int>(cur));
						g_ids = std::move(ids);
						g_haveIds = true;
					}
				}
			}
			auto waiting = std::move(g_waiting);
			g_waiting.clear();
			// if the list couldn't be fetched, carry on without sending anything this time
			if (g_haveIds) for (auto& f : waiting) f();
		});
	}

	inline void queueEvent(int lv, int p, bool c) {
		for (auto& e : g_queue) {
			if (e.lv == lv) { e.p = std::max(e.p, p); e.c = e.c || c; return; }
		}
		g_queue.push_back({ lv, p, c });
	}

	inline void flush() {
		std::string code = linkCode();
		if (code.size() != 20 || g_sending) return;
		if (g_queue.empty() && g_nowLv == 0) return;
		g_sending = true;
		std::vector<Ev> batch = g_queue;
		g_queue.clear();
		std::string body = "{\"a\":\"gdSync\",\"code\":\"" + code + "\",\"ev\":[";
		for (size_t i = 0; i < batch.size(); ++i) {
			if (i) body += ",";
			body += "{\"lv\":\"" + std::to_string(batch[i].lv) + "\",\"p\":" + std::to_string(batch[i].p) + ",\"c\":" + (batch[i].c ? "true" : "false") + "}";
		}
		body += "],\"now\":{\"lv\":\"" + std::to_string(g_nowLv) + "\",\"p\":" + std::to_string(g_nowP) + "},\"gd\":{\"n\":\"" + jsonEsc(gdName()) + "\"}}";
		auto req = web::WebRequest();
		req.header("Content-Type", "application/json");
		req.userAgent("TimmylistLive/1.0");
		req.bodyString(body);
		async::spawn(req.post(serverUrl()), [batch](web::WebResponse res) mutable {
			g_sending = false;
			if (res.ok()) {
				if (!g_greeted) {
					g_greeted = true;
					std::string b = res.string().unwrapOr("");
					std::string who;
					auto at = b.find("\"user\":\"");
					if (at != std::string::npos) {
						at += 8;
						auto end = b.find('"', at);
						if (end != std::string::npos) who = b.substr(at, end - at);
					}
					note(who.empty() ? "Timmylist Live is linked" : ("Timmylist Live linked to " + who), NotificationIcon::Success);
				}
				g_warned = false;
				return;
			}
			if (res.code() == 401) {
				if (!g_warned) {
					g_warned = true;
					note("Timmylist Live: that link code isn't valid. Make a new one on timmylist.org.", NotificationIcon::Error);
				}
				return;   // a wrong code is not worth retrying
			}
			// a network problem or a busy server: keep the events and send them with the next one
			for (auto& e : batch) queueEvent(e.lv, e.p, e.c);
			if (g_queue.size() > 60) g_queue.erase(g_queue.begin(), g_queue.begin() + (g_queue.size() - 60));
		});
	}

	inline void report(int lv, int p, bool c) {
		g_nowLv = lv;
		g_nowP = p;
		queueEvent(lv, p, c);
		flush();
	}

	inline bool counts(PlayLayer* pl) {
		if (!pl || !pl->m_level) return false;
		if (pl->m_isPracticeMode || pl->m_isTestMode) return false;
		return true;
	}

	inline void onOpen(GJGameLevel* level) {
		if (!level || linkCode().size() != 20) return;
		int lv = level->m_levelID.value();
		if (lv <= 0) return;
		int best = level->m_normalPercent.value();
		fetchIds([lv, best]() {
			if (!g_ids.count(lv)) return;
			g_best[lv] = best;
			// catch up: whatever best you already had on this level, so earlier progress is saved too
			if (best > 0) report(lv, best, best >= 100);
			else { g_nowLv = lv; g_nowP = 0; flush(); }
		});
	}

	inline void onDeath(PlayLayer* pl) {
		if (!counts(pl) || !g_haveIds || linkCode().size() != 20) return;
		int lv = pl->m_level->m_levelID.value();
		if (!g_ids.count(lv)) return;
		int pct = pl->getCurrentPercentInt();
		if (pct <= 0 || pct >= 100) return;
		auto it = g_best.find(lv);
		int known = it == g_best.end() ? 0 : it->second;
		if (pct <= known) return;
		g_best[lv] = pct;
		report(lv, pct, false);
	}

	inline void onComplete(PlayLayer* pl) {
		if (!counts(pl) || !g_haveIds || linkCode().size() != 20) return;
		int lv = pl->m_level->m_levelID.value();
		if (!g_ids.count(lv)) return;
		g_best[lv] = 100;
		report(lv, 100, true);
	}
}

class $modify(TLPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		tl::onOpen(level);
		return true;
	}

	void destroyPlayer(PlayerObject* player, GameObject* object) {
		bool anticheat = object && object == m_anticheatSpike;
		PlayLayer::destroyPlayer(player, object);
		if (!anticheat) tl::onDeath(this);
	}

	void levelComplete() {
		PlayLayer::levelComplete();
		tl::onComplete(this);
	}
};
