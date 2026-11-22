// Timmylist Live: the game hooks. Main menu button, level page badge, in-level HUD and the progress reporting.
#include "tl.hpp"
#include "popup.hpp"

#include <Geode/modify/MenuLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

$on_mod(Loaded) {
	tl::loadQueue();
}

// ---------------- main menu ----------------
class $modify(TLMenuLayer, MenuLayer) {
	bool init() {
		if (!MenuLayer::init()) return false;
		tl::start();
		if (tl::opt("menu-button")) {
			auto* menu = this->getChildByID("bottom-menu");
			if (menu) {
				auto* spr = CircleButtonSprite::createWithSprite("cube.png"_spr, 1.f, CircleBaseColor::Blue, CircleBaseSize::Medium);
				auto* btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
					tlOpenPopup();
				});
				btn->setID("timmylist-button"_spr);
				menu->addChild(btn);
				menu->updateLayout();
			}
		}
		return true;
	}
};

// ---------------- level page: rank, points, your best ----------------
class $modify(TLLevelInfoLayer, LevelInfoLayer) {
	bool init(GJGameLevel* level, bool challenge) {
		if (!LevelInfoLayer::init(level, challenge)) return false;
		if (!level) return true;
		int id = level->m_levelID.value();
		if (id <= 0) return true;
		WeakRef<LevelInfoLayer> ref = this;
		tl::fetchIds([ref, id]() {
			auto self = ref.lock();
			if (!self) return;
			auto* info = tl::infoFor(id);
			if (!info) return;   // not a Timmylist level
			auto* layer = self.data();
			auto win = CCDirector::get()->getWinSize();

			if (tl::opt("badge") && !layer->getChildByID("tl-badge"_spr)) {
				std::string t = "Timmylist #" + std::to_string(info->rank);
				if (info->pts > 0.0) t += "  |  " + tl::fmtNum(info->pts) + " pts";
				int best = tl::bestFor(id);
				if (tl::linked()) t += best >= 100 ? "  |  Completed" : (best > 0 ? "  |  your best " + std::to_string(best) + "%" : "  |  not beaten yet");
				auto* lbl = CCLabelBMFont::create(t.c_str(), "goldFont.fnt");
				lbl->setScale(0.5f);
				lbl->setID("tl-badge"_spr);
				lbl->setPosition({ win.width / 2, 14.f });
				lbl->limitLabelWidth(win.width - 40.f, 0.5f, 0.1f);
				layer->addChild(lbl, 10);
			}

			if (!layer->getChildByID("tl-level-button"_spr)) {
				auto* spr = CircleButtonSprite::createWithSprite("cube.png"_spr, 0.9f, CircleBaseColor::Blue, CircleBaseSize::Medium);
				auto* btn = CCMenuItemExt::createSpriteExtra(spr, [id](CCMenuItemSpriteExtra*) {
					web::openLinkInBrowser(tl::base() + "/#/level/" + std::to_string(id));
				});
				btn->setID("tl-level-button"_spr);
				if (auto* side = layer->getChildByID("left-side-menu")) {
					side->addChild(btn);
					side->updateLayout();
				}
				else {
					auto* menu = CCMenu::create();
					menu->setPosition({ 30.f, win.height / 2 });
					menu->addChild(btn);
					layer->addChild(menu, 10);
				}
			}
		});
		return true;
	}
};

// ---------------- in a level ----------------
namespace tl {
	inline void hud(PlayLayer* pl) {
		if (!pl || !pl->m_level || !pl->m_uiLayer) return;
		auto* old = pl->m_uiLayer->getChildByID("tl-hud"_spr);
		if (!opt("hud")) {
			if (old) old->removeFromParent();
			return;
		}
		int lv = pl->m_level->m_levelID.value();
		auto* info = infoFor(lv);
		if (!info) {
			if (old) old->removeFromParent();
			return;
		}
		std::string t = "Timmylist #" + std::to_string(info->rank);
		if (linked()) t += "  |  best " + std::to_string(bestFor(lv)) + "%";
		auto* lbl = typeinfo_cast<CCLabelBMFont*>(old);
		if (!lbl) {
			lbl = CCLabelBMFont::create(t.c_str(), "bigFont.fnt");
			lbl->setID("tl-hud"_spr);
			lbl->setScale(0.3f);
			pl->m_uiLayer->addChild(lbl, 50);
		}
		else lbl->setString(t.c_str());
		lbl->setScale(0.3f);

		float op = static_cast<float>(Mod::get()->getSettingValue<double>("hud-opacity"));
		if (op < 0.05f) op = 0.05f;
		if (op > 1.f) op = 1.f;
		lbl->setOpacity(static_cast<GLubyte>(op * 255.f));

		auto win = CCDirector::get()->getWinSize();
		std::string pos = Mod::get()->getSettingValue<std::string>("hud-position");
		float w = lbl->getScaledContentSize().width;
		float h = lbl->getScaledContentSize().height;
		float x = 6.f + w / 2;
		float y = win.height - 8.f - h / 2;
		if (pos == "Top Right") x = win.width - 6.f - w / 2;
		else if (pos == "Bottom Left") y = 8.f + h / 2;
		else if (pos == "Bottom Right") { x = win.width - 6.f - w / 2; y = 8.f + h / 2; }
		lbl->setPosition({ x, y });
	}

	inline void onOpen(PlayLayer* pl, GJGameLevel* level) {
		if (!level) return;
		int lv = level->m_levelID.value();
		if (lv <= 0) return;
		int best = level->m_normalPercent.value();
		WeakRef<PlayLayer> ref = pl;
		fetchIds([ref, lv, best]() {
			if (!infoFor(lv)) return;
			g_best[lv] = std::max(bestFor(lv), best);
			if (auto p = ref.lock()) hud(p.data());
			if (!linked()) return;
			// catch up: whatever best you already had on this level, so earlier progress is saved too
			if (best > 0 && best > (g_server.count(lv) ? g_server[lv] : 0)) report(lv, best, best >= 100);
			else { g_nowLv = lv; g_nowP = 0; flush(); }
		});
	}

	inline void onDeath(PlayLayer* pl) {
		if (!counts(pl) || !g_haveIds) return;
		int lv = pl->m_level->m_levelID.value();
		auto* info = infoFor(lv);
		if (!info) return;
		int pct = pl->getCurrentPercentInt();
		if (pct <= 0 || pct >= 100) return;
		if (pct <= bestFor(lv)) return;
		g_best[lv] = pct;
		hud(pl);
		if (!linked()) return;
		report(lv, pct, false);
		if (opt("best-toast")) note("Timmylist: new best on " + info->name + " - " + std::to_string(pct) + "%", NotificationIcon::Info);
	}

	inline void onComplete(PlayLayer* pl) {
		if (!counts(pl) || !g_haveIds) return;
		int lv = pl->m_level->m_levelID.value();
		auto* info = infoFor(lv);
		if (!info) return;
		bool first = bestFor(lv) < 100 && (!g_server.count(lv) || g_server[lv] < 100);
		g_best[lv] = 100;
		hud(pl);
		if (!linked()) return;
		if (first) {
			g_done += 1;
			g_pts += info->pts;
			g_server[lv] = 100;
			if (opt("completion-toast")) {
				std::string t = "Timmylist: " + info->name + " completed!";
				if (info->pts > 0.0) t += " +" + fmtNum(info->pts) + " points";
				note(t, NotificationIcon::Success);
			}
		}
		report(lv, 100, true);
	}
}

class $modify(TLPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
		tl::onOpen(this, level);
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
