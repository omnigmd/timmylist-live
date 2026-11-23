// Timmy Tracker: shared core. Sends your best % and completions on The Timmylist's levels to your Timmylist account,
// keeps the list of levels (name, rank, points) and your progress, and offers it to the pop-up, the badge and the HUD.
// Everything runs on the main thread. Only levels that are on the list are sent; practice and testmode are ignored.
#pragma once

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>

#include <algorithm>
#include <cstdio>
#include <ctime>
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

	struct Info {
		int id = 0;
		int rank = 0;
		double pts = 0.0;
		std::string name;
	};

	inline std::vector<Info> g_info;                      // the levels on the Timmylist, in rank order
	inline std::unordered_map<int, size_t> g_at;          // level id -> index in g_info
	inline std::unordered_map<int, int> g_best;           // best % known for a level (this device and the server)
	inline std::unordered_map<int, int> g_server;         // best % the server has saved for you
	inline std::vector<std::function<void()>> g_waiting;
	inline std::vector<Ev> g_queue;                       // events the server hasn't confirmed yet
	inline bool g_haveIds = false;
	inline bool g_fetching = false;
	inline bool g_sending = false;
	inline bool g_greeted = false;
	inline bool g_warned = false;
	inline bool g_started = false;
	inline bool g_pendingSync = false;
	inline int g_nowLv = 0;
	inline int g_nowP = 0;
	inline int g_done = 0;                                // levels you have completed
	inline int g_total = 0;                               // levels on the list
	inline double g_pts = 0.0;                            // points from your completions
	inline double g_ptsTotal = 0.0;                       // points on the whole list
	inline std::string g_user;                            // your Timmylist name
	inline long long g_lastOk = 0;                        // when the server last answered
	inline std::function<void()> g_onData;                // the open pop-up refreshes itself through this

	inline bool opt(char const* key) {
		return Mod::get()->getSettingValue<bool>(key);
	}

	inline std::string base() {
		std::string s = Mod::get()->getSettingValue<std::string>("server");
		while (!s.empty() && (s.back() == '/' || s.back() == ' ')) s.pop_back();
		if (s.empty()) s = "https://www.timmylist.org";
		return s;
	}

	inline std::string serverUrl() {
		return base() + "/api/social";
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

	inline bool linked() {
		return linkCode().size() == 20;
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

	inline std::string fmtNum(double v) {
		char b[40];
		std::snprintf(b, sizeof b, "%.2f", v);
		std::string s = b;
		while (!s.empty() && s.back() == '0') s.pop_back();
		if (!s.empty() && s.back() == '.') s.pop_back();
		return s;
	}

	inline void note(std::string const& text, NotificationIcon icon, bool force = false) {
		if (!force && !opt("notifications")) return;
		Notification::create(text, icon, 3.f)->show();
	}

	inline std::string gdName() {
		auto* acm = GJAccountManager::sharedState();
		if (!acm) return "";
		return std::string(acm->m_username);
	}

	inline Info const* infoFor(int id) {
		auto it = g_at.find(id);
		if (it == g_at.end() || it->second >= g_info.size()) return nullptr;
		return &g_info[it->second];
	}

	inline int bestFor(int id) {
		auto it = g_best.find(id);
		return it == g_best.end() ? 0 : it->second;
	}

	// ---- the queue is saved to disk, so progress made offline is sent the next time the game is open ----
	inline void saveQueue() {
		std::string s;
		for (auto const& e : g_queue) s += std::to_string(e.lv) + ":" + std::to_string(e.p) + ":" + (e.c ? "1" : "0") + ";";
		Mod::get()->setSavedValue<std::string>("queue", s);
	}

	inline void queueEvent(int lv, int p, bool c) {
		for (auto& e : g_queue) {
			if (e.lv == lv) { e.p = std::max(e.p, p); e.c = e.c || c; saveQueue(); return; }
		}
		g_queue.push_back({ lv, p, c });
		if (g_queue.size() > 200) g_queue.erase(g_queue.begin(), g_queue.begin() + (g_queue.size() - 200));
		saveQueue();
	}

	inline void loadQueue() {
		std::string s = Mod::get()->getSavedValue<std::string>("queue", "");
		size_t i = 0;
		while (i < s.size()) {
			size_t e = s.find(';', i);
			if (e == std::string::npos) e = s.size();
			int lv = 0, p = 0, c = 0;
			if (std::sscanf(s.substr(i, e - i).c_str(), "%d:%d:%d", &lv, &p, &c) == 3 && lv > 0) queueEvent(lv, p, c == 1);
			i = e + 1;
		}
	}

	inline void applyServer(matjson::Value const& v) {
		if (!v.isObject()) return;
		if (v.contains("user")) g_user = v["user"].asString().unwrapOr(g_user);
		if (v.contains("c")) g_done = static_cast<int>(v["c"].asInt().unwrapOr(g_done));
		if (v.contains("t")) g_total = static_cast<int>(v["t"].asInt().unwrapOr(g_total));
		if (v.contains("pt")) g_pts = v["pt"].asDouble().unwrapOr(g_pts);
		if (v.contains("ptt")) g_ptsTotal = v["ptt"].asDouble().unwrapOr(g_ptsTotal);
		if (v.contains("prog") && v["prog"].isObject()) {
			auto const& pr = v["prog"];
			for (auto const& in : g_info) {
				std::string k = std::to_string(in.id);
				if (!pr.contains(k)) continue;
				int p = static_cast<int>(pr[k].asInt().unwrapOr(0));
				g_server[in.id] = p;
				int& b = g_best[in.id];
				b = std::max(b, p);
			}
		}
		g_lastOk = static_cast<long long>(std::time(nullptr));
	}

	inline void fetchIds(std::function<void()> then) {
		if (g_haveIds) { then(); return; }
		g_waiting.push_back(std::move(then));
		if (g_fetching) return;
		g_fetching = true;
		auto req = web::WebRequest();
		req.header("Content-Type", "application/json");
		req.userAgent("TimmyTracker/2.0");
		req.bodyString("{\"a\":\"gdLevels\"}");
		async::spawn(req.post(serverUrl()), [](web::WebResponse res) {
			g_fetching = false;
			if (res.ok()) {
				auto v = res.json().unwrapOr(matjson::Value());
				std::vector<Info> info;
				if (v.isObject() && v.contains("lv") && v["lv"].isArray()) {
					auto const& arr = v["lv"];
					for (size_t i = 0; i < arr.size(); ++i) {
						auto const& e = arr[i];
						if (!e.isObject() || !e.contains("i")) continue;
						Info x;
						x.id = static_cast<int>(e["i"].asInt().unwrapOr(0));
						if (x.id <= 0) continue;
						x.rank = e.contains("r") ? static_cast<int>(e["r"].asInt().unwrapOr(static_cast<int>(i) + 1)) : static_cast<int>(i) + 1;
						x.pts = e.contains("p") ? e["p"].asDouble().unwrapOr(0.0) : 0.0;
						x.name = e.contains("n") ? e["n"].asString().unwrapOr("") : "";
						if (x.name.empty()) x.name = "Level " + std::to_string(x.id);
						info.push_back(std::move(x));
					}
				}
				if (info.empty() && v.isObject() && v.contains("ids") && v["ids"].isArray()) {
					auto const& arr = v["ids"];
					for (size_t i = 0; i < arr.size(); ++i) {
						Info x;
						x.id = static_cast<int>(arr[i].asInt().unwrapOr(0));
						if (x.id <= 0) continue;
						x.rank = static_cast<int>(i) + 1;
						x.name = "Level " + std::to_string(x.id);
						info.push_back(std::move(x));
					}
				}
				if (!info.empty()) {
					std::sort(info.begin(), info.end(), [](Info const& a, Info const& b) { return a.rank < b.rank; });
					g_info = std::move(info);
					g_at.clear();
					for (size_t i = 0; i < g_info.size(); ++i) g_at[g_info[i].id] = i;
					g_total = static_cast<int>(g_info.size());
					g_haveIds = true;
				}
			}
			auto waiting = std::move(g_waiting);
			g_waiting.clear();
			// if the list couldn't be fetched, carry on without sending anything this time
			if (g_haveIds) for (auto& f : waiting) f();
		});
	}

	inline int syncLocal();

	// sends what is queued (and, with `force`, just asks the server for your progress)
	inline void flush(bool force = false) {
		if (!linked() || g_sending) return;
		if (!force && g_queue.empty() && g_nowLv == 0) return;
		g_sending = true;
		std::vector<Ev> batch = g_queue;
		g_queue.clear();
		saveQueue();
		std::string body = "{\"a\":\"gdSync\",\"code\":\"" + linkCode() + "\",\"ev\":[";
		for (size_t i = 0; i < batch.size() && i < 25; ++i) {
			if (i) body += ",";
			body += "{\"lv\":\"" + std::to_string(batch[i].lv) + "\",\"p\":" + std::to_string(batch[i].p) + ",\"c\":" + (batch[i].c ? "true" : "false") + "}";
		}
		// anything beyond the 25 the server takes per request goes back in the queue
		for (size_t i = 25; i < batch.size(); ++i) g_queue.push_back(batch[i]);
		if (batch.size() > 25) batch.resize(25);
		body += "]";
		if (g_nowLv > 0) body += ",\"now\":{\"lv\":\"" + std::to_string(g_nowLv) + "\",\"p\":" + std::to_string(g_nowP) + "}";
		body += ",\"gd\":{\"n\":\"" + jsonEsc(gdName()) + "\"}}";
		auto req = web::WebRequest();
		req.header("Content-Type", "application/json");
		req.userAgent("TimmyTracker/2.0");
		req.bodyString(body);
		async::spawn(req.post(serverUrl()), [batch](web::WebResponse res) mutable {
			g_sending = false;
			if (res.ok()) {
				auto v = res.json().unwrapOr(matjson::Value());
				applyServer(v);
				if (!g_greeted) {
					g_greeted = true;
					note(g_user.empty() ? "Timmy Tracker is linked" : ("Timmy Tracker linked to " + g_user), NotificationIcon::Success);
				}
				g_warned = false;
				if (g_pendingSync) { g_pendingSync = false; syncLocal(); }
				if (g_onData) g_onData();
				if (!g_queue.empty()) flush();
				return;
			}
			if (res.code() == 401) {
				if (!g_warned) {
					g_warned = true;
					note("Timmy Tracker: that link code isn't valid. Make a new one on timmylist.org.", NotificationIcon::Error, true);
				}
				return;   // a wrong code is not worth retrying
			}
			// a network problem or a busy server: keep the events and send them with the next one
			for (auto& e : batch) queueEvent(e.lv, e.p, e.c);
		});
	}

	inline void report(int lv, int p, bool c) {
		g_nowLv = lv;
		g_nowP = p;
		queueEvent(lv, p, c);
		flush();
	}

	// the best % this device has on every level of the list, so earlier progress is saved too
	inline int syncLocal() {
		auto* glm = GameLevelManager::sharedState();
		if (!glm || !glm->m_onlineLevels || !g_haveIds) return 0;
		int n = 0;
		for (auto [key, lvl] : CCDictionaryExt<gd::string, GJGameLevel*>(glm->m_onlineLevels)) {
			if (!lvl) continue;
			int id = lvl->m_levelID.value();
			if (!g_at.count(id)) continue;
			int p = lvl->m_normalPercent.value();
			if (p <= 0) continue;
			auto sv = g_server.find(id);
			int known = sv == g_server.end() ? 0 : sv->second;
			if (p <= known) continue;
			g_best[id] = std::max(bestFor(id), p);
			queueEvent(id, p, p >= 100);
			++n;
		}
		if (n) flush();
		return n;
	}

	// once per game start: get the list, get your progress, catch up
	inline void start() {
		if (g_started) return;
		g_started = true;
		fetchIds([]() {
			if (!linked()) return;
			// ask for your progress first; once the answer is in, only the real differences are sent
			g_pendingSync = opt("sync-on-launch");
			flush(true);
		});
	}

	inline bool counts(PlayLayer* pl) {
		if (!pl || !pl->m_level) return false;
		if (pl->m_isPracticeMode || pl->m_isTestMode) return false;
		return true;
	}

	inline std::string where(Info const& in) {
		return "#" + std::to_string(in.rank) + " " + in.name;
	}

	// the Timmy Tracker logo, scaled to a height in points (falls back to nothing if the sprite is missing)
	inline CCSprite* image(std::string const& name, float height) {
		auto* spr = CCSprite::create(name.c_str());
		if (!spr) return nullptr;
		float h = spr->getContentSize().height;
		if (h > 0.f) spr->setScale(height / h);
		return spr;
	}
	inline CCSprite* logo(float height) {
		return image("logo.png"_spr, height);
	}
}
