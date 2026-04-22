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

// Layout dimensions
#define LIST_WIDTH              93      // Width of file list area
#define ROW_HEIGHT              8       // Height of each row in pixels
#define ROW_PADDING             1       // Padding inside row background
#define TEXT_PADDING            2       // Padding for text within a row

// List area background
#define LIST_LEFT               0
#define LIST_TOP                0
#define LIST_BORDER_WIDTH       1
#define LIST_BORDER_Y           57      // Y position where list ends

// Title box
#define TITLE_BOX_Y             56      // Y position of title box top
#define TITLE_BOX_TOP_HEIGHT    1       // Height of title box top border
#define TITLE_BOX_HEIGHT        7       // Height of title box content area

// Scroll indicator dimensions
#define SCROLL_INDICATOR_HEIGHT 9       // Height of scroll up/down indicator
#define SCROLL_INDICATOR_Y_OFFSET 1     // Y offset of scroll indicator content from row top
#define SCROLL_TEXT_X           2       // X position of scroll indicator text
#define SCROLL_TEXT_Y_OFFSET    2       // Y offset from row top for scroll text

// Thumbnail preview area
#define THUMBNAIL_BOX_X         92      // X position of thumbnail box
#define THUMBNAIL_BOX_Y         0       // Y position of thumbnail box
#define THUMBNAIL_BOX_WIDTH     36      // Width of thumbnail box
#define THUMBNAIL_BOX_HEIGHT    36      // Height of thumbnail box
#define THUMBNAIL_INNER_X       93      // X position of inner thumbnail area
#define THUMBNAIL_INNER_Y       1       // Y position of inner thumbnail area
#define THUMBNAIL_INNER_WIDTH   34      // Width of inner thumbnail area
#define THUMBNAIL_INNER_HEIGHT  34      // Height of inner thumbnail area

// Metadata area
#define METADATA_BOX_X          92      // X position of metadata box
#define METADATA_BOX_Y          34      // Y position of metadata box (below thumbnail)
#define METADATA_BOX_WIDTH      36      // Width of metadata box
#define METADATA_BOX_HEIGHT     22      // Height of metadata box
#define METADATA_INNER_X        93      // X position of inner metadata area
#define METADATA_INNER_Y        35      // Y position of inner metadata area
#define METADATA_INNER_WIDTH    34      // Width of inner metadata area
#define METADATA_INNER_HEIGHT   20      // Height of inner metadata area
#define METADATA_CURSOR_X       95      // X position of metadata text
#define METADATA_CURSOR_Y       43      // Y position of metadata text

// File list row styling
#define ROW_BORDER_X            0
#define ROW_BORDER_WIDTH        93
#define ROW_CONTENT_X           1       // X position of row content (inside border)
#define ROW_CONTENT_WIDTH       91      // Width of row content area
#define ROW_CONTENT_HEIGHT      7       // Height of row content area

// Text positioning within rows
#define ROW_TEXT_X              2       // X position of text start
#define ROW_TEXT_Y_OFFSET       5       // Y offset of text from row top
#define ROW_MAX_NAME_LEN        22      // Maximum characters in filename display

// Selection highlight
#define SELECTION_HIGHLIGHT_HEIGHT 9   // Height of selection highlight

// Empty list message positioning
#define EMPTY_LIST_MSG_X        5       // X position of "No .rod files" message
#define EMPTY_LIST_MSG_Y1       5       // Y position of first line
#define EMPTY_LIST_MSG_Y2       20      // Y position of second line (sad face)

// Title display
#define TITLE_DISPLAY_X         1       // X position of title text
#define TITLE_DISPLAY_Y         63      // Y position of title text

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
