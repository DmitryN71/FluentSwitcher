#pragma once
#include "utils/Images.h"
#include "utils/LetterIcons.h"
#include "utils/FluentMenu.h"

class IconMgr {
	std::filesystem::path flagFold;
	using Bundle = std::vector<Images::ImageIcon>;
	std::map<wstring, Bundle> icons;

	// Буквы вместо флага (LetterIcons.h): наборы "Letters" (EN, RU) и "Letters3" (ENG, RUS).
	static bool Letters() { return LetterIcons::Is(conf_get_unsafe()->flagsSet); }
	static bool Three() { return conf_get_unsafe()->flagsSet == LetterIcons::kThree; }
	static Images::Image ToImage(LetterIcons::Picture&& pic) {
		Images::Image img = std::make_shared<Images::details::ImageImpl>();
		if (pic.rgba.empty()) return img;
		img->data = new unsigned char[pic.rgba.size()];
		std::copy(pic.rgba.begin(), pic.rgba.end(), img->data);
		img->is_our_memory = true;
		img->width = pic.width;
		img->height = pic.height;
		img->channels = 4;
		return img;
	}
	// Значок у часов буквами: цвет - как текст панели задач (её тема - в ключе: сменилась - новый значок).
	Images::ImageIcon LetterIcon(TStr locale, Vec_i2 size, bool is_gray) {
		const bool dark = FluentMenu::TaskbarDark();
		const auto text = LetterIcons::Text(locale, Three());
		const auto key = std::format(L"letters|{}|{}x{}|{}|{}", text, size.x, size.y, dark, is_gray);
		auto it = icons.find(key);
		if (it != icons.end() && !it->second.empty()) return it->second.front();
		auto icon = Images::ImageToIconConsume(ToImage(LetterIcons::Render(text, size.x, size.y, false, dark, is_gray)));
		icons[key] = { icon };
		return icon;
	}

	// Папка набора флагов. Набора нет (удалён, как прежний "Fluent") - глянцевый.
	wstring FolderName() {
		GETCONF;
		auto folder_name = StrUtils::Convert(cfg->flagsSet);
		if (cfg->flagsSet != ProgramConfig::showFlags_AppIcon && !std::filesystem::is_directory(flagFold / folder_name)) {
			folder_name = L"Glossy";
		}
		return folder_name;
	}

	const Bundle& GetBundle(TStr local_id_, bool is_gray = false) {

		wstring local_id = local_id_;
		StrUtils::ToLower(local_id);

		GETCONF;

		auto folder_name = FolderName();
		// Британский флаг - в ключе: без него после включения настройки из кэша брался прежний, американский.
		wstring key = std::format(L"{}$&{}{}{}", local_id, folder_name, is_gray ? L"$%^&!" : L"",
			cfg->useBritishFlag ? L"$gb" : L"");

		auto it = icons.find(key);
		if (it != icons.end()) {
			return it->second;
		}

		Bundle res;
		for (auto& it : LoadImages(local_id, folder_name, is_gray)) {
			res.push_back(Images::ImageToIconConsume(it));
		}
		return icons.emplace(key, res).first->second;
	}

	// Все размеры флага языка local_id (в нижнем регистре) из набора folder_name, RGBA.
	std::vector<Images::Image> LoadImages(const wstring& local_id, const wstring& folder_name, bool is_gray) {

		namespace fs = std::filesystem;

		GETCONF;

		std::vector<Images::Image> bndl;

		auto dir = flagFold / folder_name;
		if (fs::is_directory(dir)) {

			auto search = [&](const auto& id) {
				for (const auto& entry : fs::directory_iterator(dir)) {

					if (!entry.is_directory()) 
						continue;

					auto name = entry.path().filename().wstring();
					StrUtils::ToLower(name);

					if (name != id) continue;

					for (const auto& entry2 : fs::directory_iterator(entry)) {
						if (entry2.is_regular_file()) {
							auto cur = Images::LoadImageFromFile(entry2.path().string().c_str());
							if (cur->IsOk()) {
								bndl.push_back(cur);
							}
						}
					}
				}
				};

			if (cfg->useBritishFlag && (local_id == L"en" || local_id.starts_with(L"en-"))) {
				search(L"en-gb"s);
			}
			else {
				search(local_id);

				if (bndl.empty()) {
					// ничего не нашли, попробуем поискать только по языку.
					auto pos = local_id.find(L'-');
					if (pos != std::wstring::npos) {
						SView id_lang{ local_id.data(), pos };
						search(id_lang);
					}
				}
			}

			// todo поиск только по региону


			if (is_gray) {
				// Программа выключена: флаг серый и полупрозрачный, как неактивные значки Windows.
				// Раньше он только темнел на 20 % - на тёмной панели задач разницы почти не видно.
				// Данные всегда RGBA (LoadImageFromFile грузит с STBI_rgb_alpha), channels - число каналов в файле.
				for (auto& it : bndl) {
					auto* p = it->data;
					for (int i = 0; i < it->width * it->height; i++, p += 4) {
						auto gray = (unsigned char)(0.299f * p[0] + 0.587f * p[1] + 0.114f * p[2]);
						p[0] = p[1] = p[2] = gray;
						p[3] = (unsigned char)(p[3] * 0.55f);
					}
				}
			}
		}

		return bndl;
	}
public:
	UStr folder() {
		return 0;
	}
	IconMgr() {
		flagFold = PathUtils::GetPath_folder_noLower2() / L"Flags";
	}
	static IconMgr& Inst() {
		static IconMgr inst;
		return inst;
	}

	Images::ImageIcon GetIcon(TStr contry_id, Vec_i2 size, bool is_gray = false) {
		if (Letters()) return LetterIcon(contry_id, size, is_gray);

		// приоритет: 1) все границы равны. 2) 1 граница равна, другая меньше 3) самый большой размер
		const auto& bndl = GetBundle(contry_id, is_gray);
		Images::ImageIcon pr2;
		Images::ImageIcon pr3;
		for (const auto it : bndl) {

			auto w = it->img.width;
			auto h = it->img.height;

			if (w == size.x && h == size.y) 
				return it;

			if ((w == size.x && h < size.y) || (w < size.x && h == size.y)) {
				pr2 = it;
			}
			auto sum = w + h;
			if (!pr3 || pr3->img.width + pr3->img.height < sum) {
				pr3 = it;
			}
		}

		if (pr2) return pr2;
		if (pr3) return pr3;

		return std::make_shared<Images::ImageIcon::element_type>(); // empty
	}

	// Картинка флага (RGBA) для флажка у текстового курсора: ближайшего к size размера (поровну - больший).
	// Пусто - флага нет. Не кэшируется: флажок у курсора держит свою последнюю картинку сам.
	Images::Image GetImage(TStr contry_id, int size, bool is_gray = false) {
		if (Letters()) { // буквы на плашке, высотой в три четверти размера; три буквы - шире
			const bool three = Three();
			const int w = three ? (int)std::lround(size * 1.35) : size, h = (int)std::lround(size * 0.75);
			return ToImage(LetterIcons::Render(LetterIcons::Text(contry_id, three), w, h, true, true, is_gray));
		}
		wstring local_id = contry_id;
		StrUtils::ToLower(local_id);
		Images::Image best;
		auto better = [size](const Images::Image& a, const Images::Image& b) {
			// a лучше b?
			if (!b) return true;
			int da = std::abs(a->width - size), db = std::abs(b->width - size);
			return da != db ? da < db : a->width > b->width;
		};
		for (auto& it : LoadImages(local_id, FolderName(), is_gray)) {
			if (better(it, best)) best = it;
		}
		return best;
	}

	void ClearCache() {
		icons.clear();
	}

	std::generator<string> ScanFlags() {
		namespace fs = std::filesystem;
		if (fs::is_directory(flagFold)) {
			for (const auto& entry : fs::directory_iterator(flagFold)) {
				if (entry.is_directory()) {
					co_yield entry.path().filename().string();
				}
			}
		}
	}
};
