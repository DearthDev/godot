/**************************************************************************/
/*  export_plugin.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "export_plugin.h"

#include "../logo_svg.gen.h"
#include "../run_icon_svg.gen.h"

#include "core/config/project_settings.h"
#include "editor/editor_scale.h"
#include "editor/editor_settings.h"
#include "editor/export/editor_export.h"

#include "modules/modules_enabled.gen.h" // For svg.
#ifdef MODULE_SVG_ENABLED
#include "modules/svg/image_loader_svg.h"
#endif

Error EditorExportPlatformWeb::_extract_template(const String &p_template, const String &p_dir, const String &p_name, bool pwa) {
	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);
	unzFile pkg = unzOpen2(p_template.utf8().get_data(), &io);

	if (!pkg) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Could not open template for export: \"%s\"."), p_template));
		return ERR_FILE_NOT_FOUND;
	}

	if (unzGoToFirstFile(pkg) != UNZ_OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Invalid export template: \"%s\"."), p_template));
		unzClose(pkg);
		return ERR_FILE_CORRUPT;
	}

	do {
		//get filename
		unz_file_info info;
		char fname[16384];
		unzGetCurrentFileInfo(pkg, &info, fname, 16384, nullptr, 0, nullptr, 0);

		String file = String::utf8(fname);

		// Skip folders.
		if (file.ends_with("/")) {
			continue;
		}

		// Skip service worker and offline page if not exporting pwa.
		if (!pwa && (file == "godot.service.worker.js" || file == "godot.offline.html")) {
			continue;
		}
		Vector<uint8_t> data;
		data.resize(info.uncompressed_size);

		//read
		unzOpenCurrentFile(pkg);
		unzReadCurrentFile(pkg, data.ptrw(), data.size());
		unzCloseCurrentFile(pkg);

		//write
		String dst = p_dir.path_join(file.replace("godot", p_name));
		Ref<FileAccess> f = FileAccess::open(dst, FileAccess::WRITE);
		if (f.is_null()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Could not write file: \"%s\"."), dst));
			unzClose(pkg);
			return ERR_FILE_CANT_WRITE;
		}
		f->store_buffer(data.ptr(), data.size());

	} while (unzGoToNextFile(pkg) == UNZ_OK);
	unzClose(pkg);
	return OK;
}

Error EditorExportPlatformWeb::_write_or_error(const uint8_t *p_content, int p_size, String p_path) {
	Ref<FileAccess> f = FileAccess::open(p_path, FileAccess::WRITE);
	if (f.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), p_path));
		return ERR_FILE_CANT_WRITE;
	}
	f->store_buffer(p_content, p_size);
	return OK;
}

void EditorExportPlatformWeb::_replace_strings(HashMap<String, String> p_replaces, Vector<uint8_t> &r_template) {
	String str_template = String::utf8(reinterpret_cast<const char *>(r_template.ptr()), r_template.size());
	String out;
	Vector<String> lines = str_template.split("\n");
	for (int i = 0; i < lines.size(); i++) {
		String current_line = lines[i];
		for (const KeyValue<String, String> &E : p_replaces) {
			current_line = current_line.replace(E.key, E.value);
		}
		out += current_line + "\n";
	}
	CharString cs = out.utf8();
	r_template.resize(cs.length());
	for (int i = 0; i < cs.length(); i++) {
		r_template.write[i] = cs[i];
	}
}

void EditorExportPlatformWeb::_fix_html(Vector<uint8_t> &p_html, const Ref<EditorExportPreset> &p_preset, const String &p_name, bool p_debug, int p_flags, const Vector<SharedObject> p_shared_objects, const Dictionary &p_file_sizes) {
	// Engine.js config
	Dictionary config;
	Array libs;
	for (int i = 0; i < p_shared_objects.size(); i++) {
		libs.push_back(p_shared_objects[i].path.get_file());
	}
	Vector<String> flags;
	gen_export_flags(flags, p_flags & (~DEBUG_FLAG_DUMB_CLIENT));
	Array args;
	for (int i = 0; i < flags.size(); i++) {
		args.push_back(flags[i]);
	}
	config["canvasResizePolicy"] = p_preset->get("html/canvas_resize_policy");
	config["experimentalVK"] = p_preset->get("html/experimental_virtual_keyboard");
	config["focusCanvas"] = p_preset->get("html/focus_canvas_on_start");
	config["gdextensionLibs"] = libs;
	config["executable"] = p_name;
	config["args"] = args;
	config["fileSizes"] = p_file_sizes;

	String head_include;
	if (p_preset->get("html/export_icon")) {
		head_include += "<link id='-gd-engine-icon' rel='icon' type='image/png' href='" + p_name + ".icon.png' />\n";
		head_include += "<link rel='apple-touch-icon' href='" + p_name + ".apple-touch-icon.png'/>\n";
	}
	if (p_preset->get("progressive_web_app/enabled")) {
		head_include += "<link rel='manifest' href='" + p_name + ".manifest.json'>\n";
		config["serviceWorker"] = p_name + ".service.worker.js";
	}

	// Replaces HTML string
	const String str_config = Variant(config).to_json_string();
	const String custom_head_include = p_preset->get("html/head_include");
	HashMap<String, String> replaces;
	replaces["$GODOT_URL"] = p_name + ".js";
	replaces["$GODOT_PROJECT_NAME"] = GLOBAL_GET("application/config/name");
	replaces["$GODOT_HEAD_INCLUDE"] = head_include + custom_head_include;
	replaces["$GODOT_CONFIG"] = str_config;
	_replace_strings(replaces, p_html);
}

Error EditorExportPlatformWeb::_add_manifest_icon(const String &p_path, const String &p_icon, int p_size, Array &r_arr) {
	const String name = p_path.get_file().get_basename();
	const String icon_name = vformat("%s.%dx%d.png", name, p_size, p_size);
	const String icon_dest = p_path.get_base_dir().path_join(icon_name);

	Ref<Image> icon;
	if (!p_icon.is_empty()) {
		icon.instantiate();
		const Error err = ImageLoader::load_image(p_icon, icon);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Icon Creation"), vformat(TTR("Could not read file: \"%s\"."), p_icon));
			return err;
		}
		if (icon->get_width() != p_size || icon->get_height() != p_size) {
			icon->resize(p_size, p_size);
		}
	} else {
		icon = _get_project_icon();
		icon->resize(p_size, p_size);
	}
	const Error err = icon->save_png(icon_dest);
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Icon Creation"), vformat(TTR("Could not write file: \"%s\"."), icon_dest));
		return err;
	}
	Dictionary icon_dict;
	icon_dict["sizes"] = vformat("%dx%d", p_size, p_size);
	icon_dict["type"] = "image/png";
	icon_dict["src"] = icon_name;
	r_arr.push_back(icon_dict);
	return err;
}

Error EditorExportPlatformWeb::_build_pwa(const Ref<EditorExportPreset> &p_preset, const String p_path, const Vector<SharedObject> &p_shared_objects) {
	String proj_name = GLOBAL_GET("application/config/name");
	if (proj_name.is_empty()) {
		proj_name = "Godot Game";
	}

	// Service worker
	const String dir = p_path.get_base_dir();
	const String name = p_path.get_file().get_basename();
	bool extensions = (bool)p_preset->get("variant/extensions_support");
	HashMap<String, String> replaces;
	replaces["@GODOT_VERSION@"] = String::num_int64(OS::get_singleton()->get_unix_time()) + "|" + String::num_int64(OS::get_singleton()->get_ticks_usec());
	replaces["@GODOT_NAME@"] = proj_name.substr(0, 16);
	replaces["@GODOT_OFFLINE_PAGE@"] = name + ".offline.html";

	// Files cached during worker install.
	Array cache_files;
	cache_files.push_back(name + ".html");
	cache_files.push_back(name + ".js");
	cache_files.push_back(name + ".offline.html");
	if (p_preset->get("html/export_icon")) {
		cache_files.push_back(name + ".icon.png");
		cache_files.push_back(name + ".apple-touch-icon.png");
	}
	cache_files.push_back(name + ".worker.js");
	cache_files.push_back(name + ".audio.worklet.js");
	replaces["@GODOT_CACHE@"] = Variant(cache_files).to_json_string();

	// Heavy files that are cached on demand.
	Array opt_cache_files;
	opt_cache_files.push_back(name + ".wasm");
	opt_cache_files.push_back(name + ".pck");
	if (extensions) {
		opt_cache_files.push_back(name + ".side.wasm");
		for (int i = 0; i < p_shared_objects.size(); i++) {
			opt_cache_files.push_back(p_shared_objects[i].path.get_file());
		}
	}
	replaces["@GODOT_OPT_CACHE@"] = Variant(opt_cache_files).to_json_string();

	const String sw_path = dir.path_join(name + ".service.worker.js");
	Vector<uint8_t> sw;
	{
		Ref<FileAccess> f = FileAccess::open(sw_path, FileAccess::READ);
		if (f.is_null()) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("PWA"), vformat(TTR("Could not read file: \"%s\"."), sw_path));
			return ERR_FILE_CANT_READ;
		}
		sw.resize(f->get_length());
		f->get_buffer(sw.ptrw(), sw.size());
	}
	_replace_strings(replaces, sw);
	Error err = _write_or_error(sw.ptr(), sw.size(), dir.path_join(name + ".service.worker.js"));
	if (err != OK) {
		return err;
	}

	// Custom offline page
	const String offline_page = p_preset->get("progressive_web_app/offline_page");
	if (!offline_page.is_empty()) {
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		const String offline_dest = dir.path_join(name + ".offline.html");
		err = da->copy(ProjectSettings::get_singleton()->globalize_path(offline_page), offline_dest);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("PWA"), vformat(TTR("Could not read file: \"%s\"."), offline_dest));
			return err;
		}
	}

	// Manifest
	const char *modes[4] = { "fullscreen", "standalone", "minimal-ui", "browser" };
	const char *orientations[3] = { "any", "landscape", "portrait" };
	const int display = CLAMP(int(p_preset->get("progressive_web_app/display")), 0, 4);
	const int orientation = CLAMP(int(p_preset->get("progressive_web_app/orientation")), 0, 3);

	Dictionary manifest;
	manifest["name"] = proj_name;
	manifest["start_url"] = "./" + name + ".html";
	manifest["display"] = String::utf8(modes[display]);
	manifest["orientation"] = String::utf8(orientations[orientation]);
	manifest["background_color"] = "#" + p_preset->get("progressive_web_app/background_color").operator Color().to_html(false);

	Array icons_arr;
	const String icon144_path = p_preset->get("progressive_web_app/icon_144x144");
	err = _add_manifest_icon(p_path, icon144_path, 144, icons_arr);
	if (err != OK) {
		return err;
	}
	const String icon180_path = p_preset->get("progressive_web_app/icon_180x180");
	err = _add_manifest_icon(p_path, icon180_path, 180, icons_arr);
	if (err != OK) {
		return err;
	}
	const String icon512_path = p_preset->get("progressive_web_app/icon_512x512");
	err = _add_manifest_icon(p_path, icon512_path, 512, icons_arr);
	if (err != OK) {
		return err;
	}
	manifest["icons"] = icons_arr;

	CharString cs = Variant(manifest).to_json_string().utf8();
	err = _write_or_error((const uint8_t *)cs.get_data(), cs.length(), dir.path_join(name + ".manifest.json"));
	if (err != OK) {
		return err;
	}

	return OK;
}

void EditorExportPlatformWeb::get_preset_features(const Ref<EditorExportPreset> &p_preset, List<String> *r_features) const {
	if (p_preset->get("vram_texture_compression/for_desktop")) {
		r_features->push_back("s3tc");
	}

	if (p_preset->get("vram_texture_compression/for_mobile")) {
		r_features->push_back("etc2");
	}
	r_features->push_back("wasm32");
}

void EditorExportPlatformWeb::get_export_options(List<ExportOption> *r_options) const {
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/debug", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "custom_template/release", PROPERTY_HINT_GLOBAL_FILE, "*.zip"), ""));

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "variant/extensions_support"), false)); // Export type.
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "vram_texture_compression/for_desktop"), true)); // S3TC
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "vram_texture_compression/for_mobile"), false)); // ETC or ETC2, depending on renderer

	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/export_icon"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "html/custom_html_shell", PROPERTY_HINT_FILE, "*.html"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "html/head_include", PROPERTY_HINT_MULTILINE_TEXT), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "html/canvas_resize_policy", PROPERTY_HINT_ENUM, "None,Project,Adaptive"), 2));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/focus_canvas_on_start"), true));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "html/experimental_virtual_keyboard"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::BOOL, "progressive_web_app/enabled"), false));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/offline_page", PROPERTY_HINT_FILE, "*.html"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "progressive_web_app/display", PROPERTY_HINT_ENUM, "Fullscreen,Standalone,Minimal UI,Browser"), 1));
	r_options->push_back(ExportOption(PropertyInfo(Variant::INT, "progressive_web_app/orientation", PROPERTY_HINT_ENUM, "Any,Landscape,Portrait"), 0));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/icon_144x144", PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/icon_180x180", PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::STRING, "progressive_web_app/icon_512x512", PROPERTY_HINT_FILE, "*.png,*.webp,*.svg"), ""));
	r_options->push_back(ExportOption(PropertyInfo(Variant::COLOR, "progressive_web_app/background_color", PROPERTY_HINT_COLOR_NO_ALPHA), Color()));
}

String EditorExportPlatformWeb::get_name() const {
	return "Web";
}

String EditorExportPlatformWeb::get_os_name() const {
	return "Web";
}

Ref<Texture2D> EditorExportPlatformWeb::get_logo() const {
	return logo;
}

bool EditorExportPlatformWeb::has_valid_export_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error, bool &r_missing_templates) const {
	String err;
	bool valid = false;
	bool extensions = (bool)p_preset->get("variant/extensions_support");

	// Look for export templates (first official, and if defined custom templates).
	bool dvalid = exists_export_template(_get_template_name(extensions, true), &err);
	bool rvalid = exists_export_template(_get_template_name(extensions, false), &err);

	if (p_preset->get("custom_template/debug") != "") {
		dvalid = FileAccess::exists(p_preset->get("custom_template/debug"));
		if (!dvalid) {
			err += TTR("Custom debug template not found.") + "\n";
		}
	}
	if (p_preset->get("custom_template/release") != "") {
		rvalid = FileAccess::exists(p_preset->get("custom_template/release"));
		if (!rvalid) {
			err += TTR("Custom release template not found.") + "\n";
		}
	}

	valid = dvalid || rvalid;
	r_missing_templates = !valid;

	if (!err.is_empty()) {
		r_error = err;
	}

	return valid;
}

bool EditorExportPlatformWeb::has_valid_project_configuration(const Ref<EditorExportPreset> &p_preset, String &r_error) const {
	String err;
	bool valid = true;

	// Validate the project configuration.

	if (p_preset->get("vram_texture_compression/for_mobile")) {
		String etc_error = test_etc2();
		if (!etc_error.is_empty()) {
			valid = false;
			err += etc_error;
		}
	}

	if (!err.is_empty()) {
		r_error = err;
	}

	return valid;
}

List<String> EditorExportPlatformWeb::get_binary_extensions(const Ref<EditorExportPreset> &p_preset) const {
	List<String> list;
	list.push_back("html");
	return list;
}

Error EditorExportPlatformWeb::export_project(const Ref<EditorExportPreset> &p_preset, bool p_debug, const String &p_path, int p_flags) {
	ExportNotifier notifier(*this, p_preset, p_debug, p_path, p_flags);

	const String custom_debug = p_preset->get("custom_template/debug");
	const String custom_release = p_preset->get("custom_template/release");
	const String custom_html = p_preset->get("html/custom_html_shell");
	const bool export_icon = p_preset->get("html/export_icon");
	const bool pwa = p_preset->get("progressive_web_app/enabled");

	const String base_dir = p_path.get_base_dir();
	const String base_path = p_path.get_basename();
	const String base_name = p_path.get_file().get_basename();

	// Find the correct template
	String template_path = p_debug ? custom_debug : custom_release;
	template_path = template_path.strip_edges();
	if (template_path.is_empty()) {
		bool extensions = (bool)p_preset->get("variant/extensions_support");
		template_path = find_export_template(_get_template_name(extensions, p_debug));
	}

	if (!DirAccess::exists(base_dir)) {
		return ERR_FILE_BAD_PATH;
	}

	if (!template_path.is_empty() && !FileAccess::exists(template_path)) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Prepare Templates"), vformat(TTR("Template file not found: \"%s\"."), template_path));
		return ERR_FILE_NOT_FOUND;
	}

	// Export pck and shared objects
	Vector<SharedObject> shared_objects;
	String pck_path = base_path + ".pck";
	Error error = save_pack(p_preset, p_debug, pck_path, &shared_objects);
	if (error != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), pck_path));
		return error;
	}

	{
		Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
		for (int i = 0; i < shared_objects.size(); i++) {
			String dst = base_dir.path_join(shared_objects[i].path.get_file());
			error = da->copy(shared_objects[i].path, dst);
			if (error != OK) {
				add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), shared_objects[i].path.get_file()));
				return error;
			}
		}
	}

	// Extract templates.
	error = _extract_template(template_path, base_dir, base_name, pwa);
	if (error) {
		return error;
	}

	// Parse generated file sizes (pck and wasm, to help show a meaningful loading bar).
	Dictionary file_sizes;
	Ref<FileAccess> f = FileAccess::open(pck_path, FileAccess::READ);
	if (f.is_valid()) {
		file_sizes[pck_path.get_file()] = (uint64_t)f->get_length();
	}
	f = FileAccess::open(base_path + ".wasm", FileAccess::READ);
	if (f.is_valid()) {
		file_sizes[base_name + ".wasm"] = (uint64_t)f->get_length();
	}

	// Read the HTML shell file (custom or from template).
	const String html_path = custom_html.is_empty() ? base_path + ".html" : custom_html;
	Vector<uint8_t> html;
	f = FileAccess::open(html_path, FileAccess::READ);
	if (f.is_null()) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not read HTML shell: \"%s\"."), html_path));
		return ERR_FILE_CANT_READ;
	}
	html.resize(f->get_length());
	f->get_buffer(html.ptrw(), html.size());
	f.unref(); // close file.

	// Generate HTML file with replaced strings.
	_fix_html(html, p_preset, base_name, p_debug, p_flags, shared_objects, file_sizes);
	Error err = _write_or_error(html.ptr(), html.size(), p_path);
	if (err != OK) {
		return err;
	}
	html.resize(0);

	// Export splash (why?)
	Ref<Image> splash = _get_project_splash();
	const String splash_png_path = base_path + ".png";
	if (splash->save_png(splash_png_path) != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), splash_png_path));
		return ERR_FILE_CANT_WRITE;
	}

	// Save a favicon that can be accessed without waiting for the project to finish loading.
	// This way, the favicon can be displayed immediately when loading the page.
	if (export_icon) {
		Ref<Image> favicon = _get_project_icon();
		const String favicon_png_path = base_path + ".icon.png";
		if (favicon->save_png(favicon_png_path) != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), favicon_png_path));
			return ERR_FILE_CANT_WRITE;
		}
		favicon->resize(180, 180);
		const String apple_icon_png_path = base_path + ".apple-touch-icon.png";
		if (favicon->save_png(apple_icon_png_path) != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Export"), vformat(TTR("Could not write file: \"%s\"."), apple_icon_png_path));
			return ERR_FILE_CANT_WRITE;
		}
	}

	// Generate the PWA worker and manifest
	if (pwa) {
		err = _build_pwa(p_preset, p_path, shared_objects);
		if (err != OK) {
			return err;
		}
	}

	return OK;
}

bool EditorExportPlatformWeb::poll_export() {
	Ref<EditorExportPreset> preset;

	for (int i = 0; i < EditorExport::get_singleton()->get_export_preset_count(); i++) {
		Ref<EditorExportPreset> ep = EditorExport::get_singleton()->get_export_preset(i);
		if (ep->is_runnable() && ep->get_platform() == this) {
			preset = ep;
			break;
		}
	}

	int prev = menu_options;
	menu_options = preset.is_valid();
	if (server->is_listening()) {
		if (menu_options == 0) {
			MutexLock lock(server_lock);
			server->stop();
		} else {
			menu_options += 1;
		}
	}
	return menu_options != prev;
}

Ref<ImageTexture> EditorExportPlatformWeb::get_option_icon(int p_index) const {
	return p_index == 1 ? stop_icon : EditorExportPlatform::get_option_icon(p_index);
}

int EditorExportPlatformWeb::get_options_count() const {
	return menu_options;
}

Error EditorExportPlatformWeb::run(const Ref<EditorExportPreset> &p_preset, int p_option, int p_debug_flags) {
	if (p_option == 1) {
		MutexLock lock(server_lock);
		server->stop();
		return OK;
	}

	const String dest = EditorPaths::get_singleton()->get_cache_dir().path_join("web");
	Ref<DirAccess> da = DirAccess::create(DirAccess::ACCESS_FILESYSTEM);
	if (!da->dir_exists(dest)) {
		Error err = da->make_dir_recursive(dest);
		if (err != OK) {
			add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Could not create HTTP server directory: %s."), dest));
			return err;
		}
	}

	const String basepath = dest.path_join("tmp_js_export");
	Error err = export_project(p_preset, true, basepath + ".html", p_debug_flags);
	if (err != OK) {
		// Export generates several files, clean them up on failure.
		DirAccess::remove_file_or_error(basepath + ".html");
		DirAccess::remove_file_or_error(basepath + ".offline.html");
		DirAccess::remove_file_or_error(basepath + ".js");
		DirAccess::remove_file_or_error(basepath + ".worker.js");
		DirAccess::remove_file_or_error(basepath + ".audio.worklet.js");
		DirAccess::remove_file_or_error(basepath + ".service.worker.js");
		DirAccess::remove_file_or_error(basepath + ".pck");
		DirAccess::remove_file_or_error(basepath + ".png");
		DirAccess::remove_file_or_error(basepath + ".side.wasm");
		DirAccess::remove_file_or_error(basepath + ".wasm");
		DirAccess::remove_file_or_error(basepath + ".icon.png");
		DirAccess::remove_file_or_error(basepath + ".apple-touch-icon.png");
		return err;
	}

	const uint16_t bind_port = EDITOR_GET("export/web/http_port");
	// Resolve host if needed.
	const String bind_host = EDITOR_GET("export/web/http_host");
	IPAddress bind_ip;
	if (bind_host.is_valid_ip_address()) {
		bind_ip = bind_host;
	} else {
		bind_ip = IP::get_singleton()->resolve_hostname(bind_host);
	}
	ERR_FAIL_COND_V_MSG(!bind_ip.is_valid(), ERR_INVALID_PARAMETER, "Invalid editor setting 'export/web/http_host': '" + bind_host + "'. Try using '127.0.0.1'.");

	const bool use_tls = EDITOR_GET("export/web/use_tls");
	const String tls_key = EDITOR_GET("export/web/tls_key");
	const String tls_cert = EDITOR_GET("export/web/tls_certificate");

	// Restart server.
	{
		MutexLock lock(server_lock);

		server->stop();
		err = server->listen(bind_port, bind_ip, use_tls, tls_key, tls_cert);
	}
	if (err != OK) {
		add_message(EXPORT_MESSAGE_ERROR, TTR("Run"), vformat(TTR("Error starting HTTP server: %d."), err));
		return err;
	}

	OS::get_singleton()->shell_open(String((use_tls ? "https://" : "http://") + bind_host + ":" + itos(bind_port) + "/tmp_js_export.html"));
	// FIXME: Find out how to clean up export files after running the successfully
	// exported game. Might not be trivial.
	return OK;
}

Ref<Texture2D> EditorExportPlatformWeb::get_run_icon() const {
	return run_icon;
}

void EditorExportPlatformWeb::_server_thread_poll(void *data) {
	EditorExportPlatformWeb *ej = static_cast<EditorExportPlatformWeb *>(data);
	while (!ej->server_quit) {
		OS::get_singleton()->delay_usec(6900);
		{
			MutexLock lock(ej->server_lock);
			ej->server->poll();
		}
	}
}

EditorExportPlatformWeb::EditorExportPlatformWeb() {
	if (EditorNode::get_singleton()) {
		server.instantiate();
		server_thread.start(_server_thread_poll, this);

#ifdef MODULE_SVG_ENABLED
		Ref<Image> img = memnew(Image);
		const bool upsample = !Math::is_equal_approx(Math::round(EDSCALE), EDSCALE);

		ImageLoaderSVG img_loader;
		img_loader.create_image_from_string(img, _web_logo_svg, EDSCALE, upsample, false);
		logo = ImageTexture::create_from_image(img);

		img_loader.create_image_from_string(img, _web_run_icon_svg, EDSCALE, upsample, false);
		run_icon = ImageTexture::create_from_image(img);
#endif

		Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		if (theme.is_valid()) {
			stop_icon = theme->get_icon(SNAME("Stop"), SNAME("EditorIcons"));
		} else {
			stop_icon.instantiate();
		}
	}
}

EditorExportPlatformWeb::~EditorExportPlatformWeb() {
	if (server.is_valid()) {
		server->stop();
	}
	server_quit = true;
	if (server_thread.is_started()) {
		server_thread.wait_to_finish();
	}
}
