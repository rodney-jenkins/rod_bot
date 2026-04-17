#pragma once

// apps/video_player/file_browser.h — SD card .rod file browser.
// Pushed onto the stack by VideoPlayerApp; pops itself and returns the
// chosen path via a callback when BTN_A is pressed, or does nothing on BTN_B.

/*--- INCLUDE -----------------------------------------------------------------------------------*/

#include <functional>
#include <vector>
#include <string>

#include "core/app.h"
#include "rod_format.h"


/*--- CONSTANTS ---------------------------------------------------------------------------------*/

#define UI_BUTTON_X 4
#define UI_BUTTON_Y 13
#define UI_BUTTON_Y_OFST 13
#define THUMBNAIL_X 85
#define THUMBNAIL_Y 12
#define METADATA_X 78
#define METADATA_Y 54
#define NAME_X 2
#define NAME_Y 63  

/*--- CLASSES -----------------------------------------------------------------------------------*/

class FileBrowser : public IApp
{
public:
    // `on_select` is called with the full SD path of the chosen file.
    // `on_cancel` is called if the user presses back.
    explicit FileBrowser( std::function<void( const std::string & )> on_select, std::function<void()> on_cancel = nullptr, const std::string &start_path = "/" );

    void   onEnter()  override;
    AppCmd update()   override;
    void   draw()     override;

private:
    std::function<void( const std::string & )> _on_select;
    std::function<void()>                      _on_cancel;

    struct Entry
    {
        std::string path;
        bool is_dir;
    };
    std::vector<Entry> _entries;

    int16_t _selected = 0;
    int16_t _scroll   = 0;
    bool    _dirty    = true;

    static constexpr int VISIBLE_ROWS = 7;

    void scan_sd();
    void load_preview();
    const char *basename(const std::string &path) const;

    RodHeader   _preview_hdr{};
    std::string _preview_path;
    bool        _preview_valid = false;

    std::string _cwd = "/";    // what directory is the file browser looking at?
};
