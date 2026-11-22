// The Timmylist pop-up: link status, your progress and every level on the list with your best %.
#include "tl.hpp"
#include "popup.hpp"

#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>

class TLPopup : public Popup {
protected:
	ScrollLayer* m_list = nullptr;
	CCLabelBMFont* m_status = nullptr;
	CCLabelBMFont* m_prog = nullptr;
	CCLabelBMFont* m_when = nullptr;
	CCLabelBMFont* m_empty = nullptr;

	~TLPopup() {
		tl::g_onData = nullptr;
	}

	bool init() {
		if (!Popup::init(380.f, 280.f)) return false;
		this->setTitle("Timmylist");

		auto size = m_size;

		auto cube = CCSprite::create("cube.png"_spr);
		if (cube) {
			cube->setScale(0.2f);
			cube->setPosition({ 28.f, size.height - 24.f });
			m_mainLayer->addChild(cube);
		}

		m_status = CCLabelBMFont::create("", "bigFont.fnt");
		m_status->setScale(0.42f);
		m_status->setPosition({ size.width / 2, size.height - 48.f });
		m_mainLayer->addChild(m_status);

		m_prog = CCLabelBMFont::create("", "goldFont.fnt");
		m_prog->setScale(0.5f);
		m_prog->setPosition({ size.width / 2, size.height - 68.f });
		m_mainLayer->addChild(m_prog);

		float lw = size.width - 40.f;
		float lh = 138.f;

		auto bg = CCScale9Sprite::create("square02b_001.png", { 0, 0, 80, 80 });
		bg->setContentSize({ lw + 8.f, lh + 8.f });
		bg->setColor(ccc3(0, 0, 0));
		bg->setOpacity(110);
		bg->setPosition({ size.width / 2, 66.f + lh / 2 });
		m_mainLayer->addChild(bg);

		m_list = ScrollLayer::create({ lw, lh });
		m_list->setPosition({ 20.f, 66.f });
		m_list->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout(3.f));
		m_mainLayer->addChild(m_list);

		m_empty = CCLabelBMFont::create("", "chatFont.fnt");
		m_empty->setScale(0.7f);
		m_empty->setPosition({ size.width / 2, 66.f + lh / 2 });
		m_mainLayer->addChild(m_empty);

		m_when = CCLabelBMFont::create("", "chatFont.fnt");
		m_when->setScale(0.55f);
		m_when->setOpacity(190);
		m_when->setPosition({ size.width / 2, 50.f });
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
		m_buttonMenu->addChildAtPosition(syncBtn, Anchor::Bottom, ccp(-110.f, 24.f));
		m_buttonMenu->addChildAtPosition(setBtn, Anchor::Bottom, ccp(0.f, 24.f));
		m_buttonMenu->addChildAtPosition(webBtn, Anchor::Bottom, ccp(110.f, 24.f));

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

	CCNode* makeRow(tl::Info const& in, float w) {
		int best = tl::bestFor(in.id);
		bool done = best >= 100;
		float h = 24.f;

		auto inner = CCNode::create();
		inner->setContentSize({ w, h });
		inner->setAnchorPoint({ 0.5f, 0.5f });

		auto bg = CCScale9Sprite::create("square02b_001.png", { 0, 0, 80, 80 });
		bg->setContentSize({ w, h });
		bg->setColor(done ? ccc3(20, 110, 60) : ccc3(20, 50, 120));
		bg->setOpacity(done ? 200 : 170);
		bg->setPosition({ w / 2, h / 2 });
		inner->addChild(bg);

		auto rank = CCLabelBMFont::create(("#" + std::to_string(in.rank)).c_str(), "bigFont.fnt");
		rank->setScale(0.38f);
		rank->setAnchorPoint({ 0.f, 0.5f });
		rank->setPosition({ 8.f, h / 2 });
		inner->addChild(rank);

		std::string nm = in.name;
		if (nm.size() > 24) nm = nm.substr(0, 23) + "...";
		auto name = CCLabelBMFont::create(nm.c_str(), "bigFont.fnt");
		name->setScale(0.38f);
		name->setAnchorPoint({ 0.f, 0.5f });
		name->setPosition({ 46.f, h / 2 });
		inner->addChild(name);

		std::string right = best > 0 ? (std::to_string(best) + "%") : "-";
		auto pct = CCLabelBMFont::create(right.c_str(), "bigFont.fnt");
		pct->setScale(0.38f);
		pct->setAnchorPoint({ 1.f, 0.5f });
		pct->setPosition({ w - 8.f, h / 2 });
		pct->setColor(done ? ccc3(130, 255, 170) : (best > 0 ? ccc3(255, 255, 255) : ccc3(150, 160, 190)));
		inner->addChild(pct);

		int id = in.id;
		auto btn = CCMenuItemExt::createSpriteExtra(inner, [id](CCMenuItemSpriteExtra*) {
			TLPopup::openLevel(id);
		});
		btn->setPosition({ w / 2, h / 2 });

		auto menu = CCMenu::create();
		menu->setContentSize({ w, h });
		menu->ignoreAnchorPointForPosition(false);
		menu->addChild(btn);
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
		m_status->limitLabelWidth(m_size.width - 60.f, 0.42f, 0.1f);

		std::string p = "Completed " + std::to_string(tl::g_done) + " / " + std::to_string(tl::g_total > 0 ? tl::g_total : static_cast<int>(tl::g_info.size())) + " levels";
		if (tl::g_ptsTotal > 0.0) p += "   " + tl::fmtNum(tl::g_pts) + " / " + tl::fmtNum(tl::g_ptsTotal) + " pts";
		m_prog->setString(p.c_str());
		m_prog->limitLabelWidth(m_size.width - 40.f, 0.5f, 0.1f);

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
		m_empty->setVisible(false);
		float rw = m_size.width - 40.f - 6.f;
		for (auto const& in : tl::g_info) m_list->m_contentLayer->addChild(this->makeRow(in, rw));
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
