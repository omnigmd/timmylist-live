// The Timmy Tracker pop-up: link status, your progress and every level on the list with your best %.
#include "tl.hpp"
#include "popup.hpp"
#include "replay.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/ui/General.hpp>
#include <Geode/ui/MDPopup.hpp>

class TLPopup : public Popup {
protected:
	ScrollLayer* m_list = nullptr;
	CCLabelBMFont* m_status = nullptr;
	CCLabelBMFont* m_prog = nullptr;
	CCLabelBMFont* m_when = nullptr;
	CCLabelBMFont* m_empty = nullptr;
	CCNode* m_barHolder = nullptr;
	float m_barWidth = 0.f;
	ButtonSprite* m_filterSpr[3] = { nullptr, nullptr, nullptr };
	int m_filter = 0;   // 0 all, 1 to do, 2 done

	~TLPopup() {
		tl::g_onData = nullptr;
	}

	bool init() {
		if (!Popup::init(400.f, 310.f)) return false;
		this->setTitle("Timmy Tracker");

		auto size = m_size;

		// logo and info button in the top right, like other mods
		auto logo = tl::logo(34.f);
		if (logo) {
			logo->setPosition({ size.width - 58.f, size.height - 22.f });
			m_mainLayer->addChild(logo);
		}
		auto infoSpr = CCSprite::createWithSpriteFrameName("GJ_infoIcon_001.png");
		infoSpr->setScale(0.8f);
		auto infoBtn = CCMenuItemExt::createSpriteExtra(infoSpr, [](CCMenuItemSpriteExtra*) {
			MDPopup::create(
				"Timmy Tracker",
				"Timmy Tracker sends your **best %** and **completions** on [The Timmylist](https://www.timmylist.org) to your account while you play.\n\n"
				"- Link it once with the code from **Settings, Connected Accounts, Geometry Dash** on timmylist.org.\n"
				"- Only levels on the list are sent. Practice mode and testmode are ignored.\n"
				"- Progress made offline is saved and sent the next time the game is open.\n"
				"- **Sync** sends the best % you already had, so earlier progress is not lost.\n"
				"- Tap a level in the list to open it.\n"
					"- Beat a level and your run is saved. Tap the green play button on its row or on its level page to watch it back.",
				"OK"
			)->show();
		});
		m_buttonMenu->addChildAtPosition(infoBtn, Anchor::TopRight, ccp(-20.f, -20.f));

		m_status = CCLabelBMFont::create("", "bigFont.fnt");
		m_status->setPosition({ size.width / 2, size.height - 46.f });
		m_mainLayer->addChild(m_status);

		m_prog = CCLabelBMFont::create("", "goldFont.fnt");
		m_prog->setPosition({ size.width / 2, size.height - 64.f });
		m_mainLayer->addChild(m_prog);

		// overall progress bar (the game's own progress bar sprite)
		m_barWidth = 250.f;
		m_barHolder = CCNode::create();
		m_barHolder->setPosition({ size.width / 2, size.height - 80.f });
		m_mainLayer->addChild(m_barHolder);

		// filters
		char const* names[3] = { "All", "To do", "Done" };
		float fx[3] = { -80.f, 0.f, 80.f };
		for (int i = 0; i < 3; ++i) {
			m_filterSpr[i] = ButtonSprite::create(names[i], 64, true, "bigFont.fnt", i == 0 ? "GJ_button_02.png" : "GJ_button_04.png", 24.f, .55f);
			auto b = CCMenuItemExt::createSpriteExtra(m_filterSpr[i], [this, i](CCMenuItemSpriteExtra*) {
				m_filter = i;
				for (int k = 0; k < 3; ++k) m_filterSpr[k]->updateBGImage(k == i ? "GJ_button_02.png" : "GJ_button_04.png");
				this->refresh();
			});
			m_buttonMenu->addChildAtPosition(b, Anchor::Top, ccp(fx[i], -102.f));
		}

		float lw = size.width - 40.f;
		float lh = 124.f;
		float ly = 66.f;

		auto bg = CCScale9Sprite::create("square02b_001.png", { 0, 0, 80, 80 });
		bg->setContentSize({ lw, lh });
		bg->setColor(ccc3(0, 0, 0));
		bg->setOpacity(90);
		bg->setPosition({ size.width / 2, ly + lh / 2 });
		m_mainLayer->addChild(bg);

		m_list = ScrollLayer::create({ lw, lh });
		m_list->setPosition({ 20.f, ly });
		m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(2.f));
		m_mainLayer->addChild(m_list);

		addListBorders(m_mainLayer, { size.width / 2, ly + lh / 2 }, { lw, lh });

		m_empty = CCLabelBMFont::create("", "chatFont.fnt");
		m_empty->setScale(0.7f);
		m_empty->setPosition({ size.width / 2, ly + lh / 2 });
		m_mainLayer->addChild(m_empty);

		m_when = CCLabelBMFont::create("", "chatFont.fnt");
		m_when->setScale(0.55f);
		m_when->setOpacity(190);
		m_when->setPosition({ size.width / 2, 52.f });
		m_mainLayer->addChild(m_when);

		auto syncBtn = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Sync", 70, true, "goldFont.fnt", "GJ_button_01.png", 28.f, .7f), [this](CCMenuItemSpriteExtra*) {
			this->onSync();
		});
		auto setBtn = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Settings", 90, true, "goldFont.fnt", "GJ_button_04.png", 28.f, .7f), [](CCMenuItemSpriteExtra*) {
			openSettingsPopup(Mod::get());
		});
		auto webBtn = CCMenuItemExt::createSpriteExtra(ButtonSprite::create("Website", 90, true, "goldFont.fnt", "GJ_button_02.png", 28.f, .7f), [](CCMenuItemSpriteExtra*) {
			web::openLinkInBrowser(tl::base());
		});
		m_buttonMenu->addChildAtPosition(syncBtn, Anchor::Bottom, ccp(-115.f, 26.f));
		m_buttonMenu->addChildAtPosition(setBtn, Anchor::Bottom, ccp(0.f, 26.f));
		m_buttonMenu->addChildAtPosition(webBtn, Anchor::Bottom, ccp(115.f, 26.f));

		WeakRef<TLPopup> ref = this;
		tl::g_onData = [ref]() {
			if (auto p = ref.lock()) p->refresh();
		};
		this->refresh();
		tl::fetchIds([ref]() {
			if (auto p = ref.lock()) p->refresh();
		});
		if (tl::linked()) tl::flush(true);
		return true;
	}

	void onSync() {
		if (!tl::linked()) {
			tl::note("Paste your link code in the settings first.", NotificationIcon::Warning, true);
			return;
		}
		int n = tl::syncLocal();
		tl::flush(true);
		tl::note(n > 0 ? ("Sending your progress on " + std::to_string(n) + " level(s)") : "Syncing with Timmylist...", NotificationIcon::Info, true);
	}

	static void openLevel(int id) {
		auto search = GJSearchObject::create(SearchType::Search, std::to_string(id));
		auto scene = LevelBrowserLayer::scene(search);
		CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
	}

	// the same white progress bar sprite the level page uses, tinted and clipped
	static CCNode* gdBar(float w, float h, int pct, ccColor3B col) {
		auto root = CCNode::create();
		root->setContentSize({ w, h });
		auto back = CCSprite::create("GJ_progressBar_001.png");
		if (!back) return root;
		auto sz = back->getContentSize();
		if (sz.width <= 0.f || sz.height <= 0.f) return root;
		float sx = w / sz.width, sy = h / sz.height;
		back->setColor(ccc3(0, 0, 0));
		back->setOpacity(130);
		back->setScaleX(sx);
		back->setScaleY(sy);
		root->addChild(back);
		if (pct > 0) {
			auto front = CCSprite::create("GJ_progressBar_001.png");
			if (front) {
				float f = static_cast<float>(std::min(pct, 100)) / 100.f;
				front->setColor(col);
				front->setAnchorPoint({ 0.f, 0.5f });
				front->setTextureRect(CCRect(0.f, 0.f, std::max(2.f, sz.width * f), sz.height));
				front->setScaleX(sx * 0.985f);
				front->setScaleY(sy * 0.8f);
				front->setPosition({ -w / 2 + w * 0.0075f, 0.f });
				root->addChild(front);
			}
		}
		return root;
	}

	static CCSprite* frameIcon(char const* name, float height) {
		auto spr = CCSprite::createWithSpriteFrameName(name);
		if (!spr) return nullptr;
		float h = spr->getContentSize().height;
		if (h > 0.f) spr->setScale(height / h);
		return spr;
	}

	CCNode* makeRow(tl::Info const& in, float w, int index) {
		int best = tl::bestFor(in.id);
		bool done = best >= 100;
		float h = 30.f;

		auto menu = CCMenu::create();
		menu->setContentSize({ w, h });
		menu->ignoreAnchorPointForPosition(false);
		menu->setPosition({ 0.f, 0.f });

		// the game's own list cell colours
		auto bg = CCLayerColor::create(index % 2 == 0 ? ccc4(194, 114, 62, 255) : ccc4(161, 88, 44, 255), w, h);
		bg->setPosition({ 0.f, 0.f });
		menu->addChild(bg);

		// extreme demon face from the game's own sheet, dim until you beat the level
		if (auto face = frameIcon("diffIcon_10_btn_001.png", 22.f)) {
			face->setPosition({ 18.f, h / 2 });
			if (!done) {
				face->setColor(ccc3(150, 150, 150));
				face->setOpacity(170);
			}
			menu->addChild(face);
		}

		auto rank = CCLabelBMFont::create(("#" + std::to_string(in.rank)).c_str(), "goldFont.fnt");
		rank->setScale(0.45f);
		rank->setAnchorPoint({ 0.f, 0.5f });
		rank->setPosition({ 36.f, h / 2 });
		menu->addChild(rank);

		auto name = CCLabelBMFont::create(in.name.c_str(), "bigFont.fnt");
		name->setAnchorPoint({ 0.f, 0.5f });
		name->setPosition({ 74.f, h / 2 });
		name->limitLabelWidth(w - 74.f - 178.f, 0.4f, 0.1f);
		menu->addChild(name);

		// progress bar, with the game's check mark in front of it once the level is beaten
		float bw = 78.f, bx = w - 52.f - bw;
		auto bar = gdBar(bw, 10.f, best, done ? ccc3(70, 230, 120) : ccc3(46, 134, 255));
		bar->setPosition({ bx + bw / 2, h / 2 });
		menu->addChild(bar);
		if (done) {
			if (auto chk = frameIcon("GJ_completesIcon_001.png", 16.f)) {
				chk->setPosition({ bx - 12.f, h / 2 });
				menu->addChild(chk);
			}
		}

		std::string right = best > 0 ? (std::to_string(best) + "%") : "-";
		auto pct = CCLabelBMFont::create(right.c_str(), "bigFont.fnt");
		pct->setScale(0.4f);
		pct->setAnchorPoint({ 1.f, 0.5f });
		pct->setPosition({ w - 8.f, h / 2 });
		pct->setColor(done ? ccc3(170, 255, 190) : (best > 0 ? ccc3(255, 255, 255) : ccc3(255, 235, 215)));
		menu->addChild(pct);

		int id = in.id;
		// a green play button once a run is saved for this level
		bool hasReplay = tl::rp::exists(id);
		float rx = bx - 36.f;
		if (hasReplay) {
			if (auto play = frameIcon("GJ_playBtn2_001.png", 20.f)) {
				auto rb = CCMenuItemExt::createSpriteExtra(play, [id](CCMenuItemSpriteExtra*) {
					tl::rp::watch(id);
				});
				rb->setPosition({ rx, h / 2 });
				menu->addChild(rb);
			}
		}
		// the rest of the row opens the level
		auto addHit = [&](float x0, float x1) {
			if (x1 - x0 < 4.f) return;
			auto hit = CCNode::create();
			hit->setContentSize({ x1 - x0, h });
			hit->setAnchorPoint({ 0.5f, 0.5f });
			auto btn = CCMenuItemExt::createSpriteExtra(hit, [id](CCMenuItemSpriteExtra*) {
				TLPopup::openLevel(id);
			});
			btn->m_animationEnabled = false;
			btn->setPosition({ (x0 + x1) / 2, h / 2 });
			menu->addChild(btn);
		};
		if (hasReplay) {
			addHit(0.f, rx - 14.f);
			addHit(rx + 14.f, w);
		}
		else addHit(0.f, w);
		return menu;
	}

public:
	void refresh() {
		if (!m_list || !m_status) return;

		if (!tl::linked()) {
			m_status->setString("Not linked. Paste your link code in Settings.");
			m_status->setColor(ccc3(255, 120, 120));
		}
		else if (!tl::g_user.empty()) {
			m_status->setString(("Linked to " + tl::g_user).c_str());
			m_status->setColor(ccc3(130, 255, 170));
		}
		else {
			m_status->setString("Linking...");
			m_status->setColor(ccc3(255, 225, 120));
		}
		m_status->setScale(0.45f);
		m_status->limitLabelWidth(m_size.width - 60.f, 0.45f, 0.1f);

		int total = tl::g_total > 0 ? tl::g_total : static_cast<int>(tl::g_info.size());
		std::string p = "Completed " + std::to_string(tl::g_done) + " / " + std::to_string(total) + " levels";
		if (tl::g_ptsTotal > 0.0) p += "   " + tl::fmtNum(tl::g_pts) + " / " + tl::fmtNum(tl::g_ptsTotal) + " pts";
		m_prog->setString(p.c_str());
		m_prog->setScale(0.5f);
		m_prog->limitLabelWidth(m_size.width - 40.f, 0.5f, 0.1f);

		float ratio = total > 0 ? std::min(1.f, static_cast<float>(tl::g_done) / static_cast<float>(total)) : 0.f;
		m_barHolder->removeAllChildren();
		m_barHolder->addChild(gdBar(m_barWidth, 14.f, static_cast<int>(ratio * 100.f + 0.5f), ccc3(70, 230, 120)));

		std::string w;
		if (tl::g_lastOk > 0) {
			long long d = static_cast<long long>(std::time(nullptr)) - tl::g_lastOk;
			if (d < 10) w = "Synced just now";
			else if (d < 90) w = "Synced " + std::to_string(d) + " seconds ago";
			else w = "Synced " + std::to_string(d / 60) + " minutes ago";
		}
		else if (tl::linked()) w = "Waiting for Timmylist...";
		size_t queued = tl::g_queue.size();
		if (queued) w += (w.empty() ? "" : "  |  ") + std::to_string(queued) + " waiting to send";
		m_when->setString(w.c_str());

		m_list->m_contentLayer->removeAllChildren();
		if (tl::g_info.empty()) {
			m_empty->setString(tl::g_haveIds ? "No levels yet." : "Loading the list...");
			m_empty->setVisible(true);
			return;
		}
		float rw = m_size.width - 40.f - 4.f;
		int shown = 0;
		for (auto const& in : tl::g_info) {
			bool done = tl::bestFor(in.id) >= 100;
			if (m_filter == 1 && done) continue;
			if (m_filter == 2 && !done) continue;
			m_list->m_contentLayer->addChild(this->makeRow(in, rw, shown));
			++shown;
		}
		m_empty->setString(m_filter == 2 ? "Nothing completed yet." : "Nothing left to do.");
		m_empty->setVisible(shown == 0);
		m_list->m_contentLayer->updateLayout();
		m_list->scrollToTop();
	}

	static TLPopup* create() {
		auto ret = new TLPopup();
		if (ret->init()) {
			ret->autorelease();
			return ret;
		}
		delete ret;
		return nullptr;
	}
};

void tlOpenPopup() {
	if (auto p = TLPopup::create()) p->show();
}
