#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <wchar.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <vector>
#include <string>

#define NELEMENTS(buf) (sizeof(buf) / sizeof(buf[0]))

class TextView {
public:
    TextView();

    void create(Display *display, Window parent, int inputStyle);
    void destroy();
    void setTitle(const char *title);

    bool isDestroyed();

    void onExpose();
    void onFocusIn();
    void onFocusOut();
    void onKeyPress(XKeyPressedEvent *event);
    void onClientMessage(const XClientMessageEvent *event);

    // for XIM
    void registerInstantiateCallback();

    // IM instantiate callback
    static void IMInstantiateCallback(Display *display, XPointer client_data, XPointer data);
    // IM destroy callbacks
    static void IMDestroyCallback(XIM xim, XPointer client_data, XPointer data);

    // on the spot callbacks
    static void preeditStartCallback(XIM xim, XPointer client_data, XPointer data);
    static void preeditDoneCallback(XIM xim, XPointer client_data, XPointer data);
    static void preeditDrawCallback(XIM xim, XPointer client_data, XPointer data);
    static void preeditCaretCallback(XIM xim, XPointer client_data, XPointer data);

private:
    // for XIM interaction
    void openIM();
    void closeIM();
    void createIC();
    void destroyIC();

    void setPreeditString(const wchar_t *str, int pos, int length);
    void setPreeditCaret(int pos);
    void getSpotLocation(XPoint &point);
    void updateSpotLocation();

    // keyevent
    void onKeyReturn();
    void onKeyDelete();
    void onKeyBackspace();
    void onKeyHome();
    void onKeyEnd();
    void onKeyEscape();

    void insert(const wchar_t *str);
    void moveCaret(int x, int y);
    void draw();

    static std::string fontsetString;

    Display *m_display;
    Window m_window;
    GC m_gc;
    GC m_preeditGC;

    XIM m_im;
    XIC m_ic;
    Atom m_WMDeleteWindow;
    int m_width;
    int m_height;

    XFontSet m_fontset;
    XRectangle m_fontsetRect;

    int m_inputStyle;
    
    int m_nrow;
    int m_ncol;
    XPoint m_caret; // coord. in row/col

    std::wstring m_preeditString;
    int m_preeditCaret;

    std::vector<std::wstring*> m_text;
};

std::string TextView::fontsetString = "*,*";

TextView::TextView() :
    m_display(NULL),
    m_window(0),
    m_gc(0),
    m_preeditGC(0),
    m_ic(0),
    m_width(300),
    m_height(200),
    m_fontset(NULL),
    m_nrow(10),
    m_ncol(80),
    m_preeditCaret(0)
{
    m_fontsetRect.x = 0;
    m_fontsetRect.y = 0;
    m_fontsetRect.width = 0;
    m_fontsetRect.height = 0;

    m_caret.x = 0;
    m_caret.y = 0;

    m_text.push_back(new std::wstring(L""));
}

bool TextView::isDestroyed()
{
    return m_window == 0;
}

void TextView::registerInstantiateCallback()
{
    XRegisterIMInstantiateCallback(m_display, NULL, NULL, NULL,
		       TextView::IMInstantiateCallback, (XPointer)this);
}

void TextView::openIM()
{
    m_im = XOpenIM(m_display, NULL, NULL, NULL);
    if (m_im  == NULL) {
	printf("Can't open XIM\n");
	return;
    }

    printf("XIM is opened\n");
    XUnregisterIMInstantiateCallback(m_display, NULL, NULL, NULL,
				     IMInstantiateCallback, (XPointer)this);

    // register destroy callback
    XIMCallback destroy;
    destroy.callback = IMDestroyCallback;
    destroy.client_data = (XPointer)this;
    XSetIMValues(m_im, XNDestroyCallback, &destroy, NULL);

    createIC();
}

void TextView::closeIM(void)
{
    XCloseIM(m_im);
    m_im = NULL;

    printf("XIM is closed\n");
}

void TextView::createIC(void)
{
    if ((m_inputStyle & XIMPreeditCallbacks) == XIMPreeditCallbacks) {
	XIMCallback preedit_start;
	XIMCallback preedit_done;
	XIMCallback preedit_draw;
	preedit_start.callback = preeditStartCallback;
	preedit_start.client_data = (XPointer)this;
	preedit_done.callback = preeditDoneCallback;
	preedit_done.client_data = (XPointer)this;
	preedit_draw.callback = preeditDrawCallback;
	preedit_draw.client_data = (XPointer)this;
	XVaNestedList attr = XVaCreateNestedList(0,
				 XNPreeditStartCallback, &preedit_start,
				 XNPreeditDoneCallback,  &preedit_done,
				 XNPreeditDrawCallback,  &preedit_draw,
				 NULL);
	m_ic = XCreateIC(m_im, 
			 XNInputStyle, XIMPreeditCallbacks,
			 XNClientWindow, m_window,
			 XNPreeditAttributes, attr,
			 NULL);
	XFree(attr);
    } else if ((m_inputStyle & XIMPreeditPosition) == XIMPreeditPosition) {
	XRectangle area;
	area.x = 0;
	area.y = 0;
	area.width = m_width;
	area.height = m_height;

	XPoint spotLocation;
	getSpotLocation(spotLocation);

	XVaNestedList attr = XVaCreateNestedList(0,
					     XNSpotLocation, &spotLocation,
					     XNArea, &area,
					     XNFontSet, m_fontset,
					     NULL);
	m_ic = XCreateIC(m_im, 
			 XNInputStyle, XIMPreeditPosition,
			 XNClientWindow, m_window,
			 XNPreeditAttributes, attr,
			 NULL);
	XFree(attr);
    } else if ((m_inputStyle & XIMPreeditArea) == XIMPreeditArea) {
	XRectangle area;
	area.x = 0;
	area.y = 0;
	area.width = m_width;
	area.height = m_height;
	XVaNestedList attr = XVaCreateNestedList(0,
							  XNArea, &area,
							  XNFontSet, m_fontset,
							  NULL);
	m_ic = XCreateIC(m_im, 
			 XNInputStyle, XIMPreeditArea,
			 XNClientWindow, m_window,
			 XNPreeditAttributes, attr,
			 NULL);
	XFree(attr);
    } else if ((m_inputStyle & XIMPreeditNothing) == XIMPreeditNothing) {
	m_ic = XCreateIC(m_im, 
			 XNInputStyle, XIMPreeditNothing,
			 XNClientWindow, m_window,
			 NULL);
    } else if ((m_inputStyle & XIMPreeditNone) == XIMPreeditNone) {
	m_ic = XCreateIC(m_im, 
			 XNInputStyle, XIMPreeditNone,
			 XNClientWindow, m_window,
			 NULL);
    }

    unsigned long fevent = 0;
    XGetICValues(m_ic, XNFilterEvents, &fevent, NULL);
    unsigned long mask = ExposureMask | KeyPressMask | FocusChangeMask;
    XSelectInput(m_display, m_window, mask | fevent);

    printf("XIC is created\n");
}

void TextView::destroyIC()
{
    if (m_ic != NULL) {
	XDestroyIC(m_ic);
	m_ic = NULL;
	printf("XIC is destroyed\n");
    }
}

void TextView::create(Display *display, Window parent, int inputStyle)
{
    // create window
    m_display = display;
    int screen = DefaultScreen(m_display);
    if (parent == 0)
	parent = RootWindow(display, screen);
    m_window = XCreateSimpleWindow(m_display, parent,
				  0, 0, m_width, m_height, 0,
				  BlackPixel(m_display, screen),
				  WhitePixel(m_display, screen));

    // load fontset
    char **missing_list = NULL;
    int missing_count = 0;
    char *default_string = NULL;
    m_fontset = XCreateFontSet(display, fontsetString.c_str(),
			 &missing_list, &missing_count, &default_string);
    if (m_fontset == NULL) {
	printf("Can't create font set: %s\n", fontsetString.c_str());
	exit(0);
    }

    XFontStruct **font_struct;
    char **font_names;
    int n = XFontsOfFontSet(m_fontset, &font_struct, &font_names);
    if (n > 0) {
	printf("fonset:");
	for (int i = 0; i < n; i++) {
	    printf("\t%s\n", font_names[i]);
	}
    }

    XFontSetExtents *fontset_ext = XExtentsOfFontSet(m_fontset);
    m_fontsetRect = fontset_ext->max_logical_extent;

    // create GC
    XGCValues values;
    values.foreground = BlackPixel(m_display, screen);
    values.background = WhitePixel(m_display, screen);
    m_gc = XCreateGC(display, m_window,
		     GCForeground | GCBackground, &values);
    values.foreground = WhitePixel(m_display, screen);
    values.background = BlackPixel(m_display, screen);
    m_preeditGC = XCreateGC(display, m_window,
			    GCForeground | GCBackground, &values);

    // set input style
    m_inputStyle = inputStyle;

    // register instantiate callback
    registerInstantiateCallback();

    unsigned long mask = ExposureMask | KeyPressMask | KeyReleaseMask | FocusChangeMask;
    XSelectInput(m_display, m_window, mask);

    m_WMDeleteWindow = XInternAtom(m_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(m_display, m_window, &m_WMDeleteWindow, 1);

    XMapWindow(m_display, m_window);
}

void TextView::setTitle(const char *title)
{
    if (m_window != 0)
	XStoreName(m_display, m_window, title);
}

void TextView::destroy()
{
    for (std::vector<std::wstring*>::iterator iter = m_text.begin();
	 iter != m_text.end();
	 ++iter) {
	delete (*iter);
    }

    destroyIC();
    closeIM();

    XDestroyWindow(m_display, m_window);
    m_window = 0;
    m_display = NULL;
}

void TextView::draw()
{
    XClearWindow(m_display, m_window);

    int i, x = 0, y = 0;
    for (i = 0; i < (int)m_text.size(); i++) {
	if (i == m_caret.y) {
	    int xpos = 0;
	    std::wstring before = m_text[i]->substr(0, m_caret.x);
	    std::wstring after = m_text[i]->substr(m_caret.x,
					m_text[i]->size() - m_caret.x);
	    XwcDrawImageString(m_display, m_window, m_fontset, m_gc,
			       xpos, y - m_fontsetRect.y,
			       before.c_str(), before.size());
	    xpos += XwcTextEscapement(m_fontset, before.c_str(), before.size());

	    XwcDrawImageString(m_display, m_window, m_fontset, m_preeditGC,
			       xpos, y - m_fontsetRect.y,
			       m_preeditString.c_str(), m_preeditString.size());
	    xpos += XwcTextEscapement(m_fontset, m_preeditString.c_str(), m_preeditString.size());

	    XwcDrawImageString(m_display, m_window, m_fontset, m_gc,
			       xpos, y - m_fontsetRect.y,
			       after.c_str(), after.size());
	} else {
	    XwcDrawImageString(m_display, m_window, m_fontset, m_gc,
			       x, y - m_fontsetRect.y,
			       m_text[i]->c_str(), m_text[i]->size());
	}
	y += m_fontsetRect.height;
    }

    // draw caret;
    std::wstring fullstring = *m_text[m_caret.y];
    fullstring.insert(m_caret.x, m_preeditString);
    std::wstring substring = fullstring.substr(0, m_caret.x + m_preeditCaret);
    x = XwcTextEscapement(m_fontset, substring.c_str(), substring.size());
    y = m_fontsetRect.height * m_caret.y;
    XDrawLine(m_display, m_window, m_gc, x, y, x, y + m_fontsetRect.height);
}

void TextView::insert(const wchar_t *str)
{
    m_text[m_caret.y]->insert(m_caret.x, str);
    m_caret.x += wcslen(str);
}

void TextView::moveCaret(int x, int y)
{
    if (x > 0) {
	if (m_caret.x < (int)m_text[m_caret.y]->size()) {
	    m_caret.x++;
	} else if (m_caret.y < (int)m_text.size() - 1) {
	    m_caret.x = 0;
	    m_caret.y++;
	}
    } else if (x < 0) {
	if (m_caret.x > 0) {
	    m_caret.x--;
	} else if (m_caret.y > 0) {
	    m_caret.x = (int)m_text[m_caret.y - 1]->size();
	    m_caret.y--;
	}
    }

    if (y > 0) {
	if (m_caret.y < (int)m_text.size() - 1) {
	    m_caret.y++;
	    if (m_caret.x > (int)m_text[m_caret.y]->size())
		m_caret.x = m_text[m_caret.y]->size();
	}
    } else if (y < 0) {
	if (m_caret.y > 0) {
	    m_caret.y--;
	    if (m_caret.x > (int)m_text[m_caret.y]->size())
		m_caret.x = m_text[m_caret.y]->size();
	}
    }

    updateSpotLocation();
}

void TextView::updateSpotLocation()
{
    if (m_ic == NULL)
	return;

    if ((m_inputStyle | XIMPreeditPosition) == XIMPreeditPosition) {
	XPoint spotLocation = { 0, 0 };
	getSpotLocation(spotLocation);

	XVaNestedList attr = XVaCreateNestedList(0,
					XNSpotLocation, &spotLocation,
					NULL);
	XSetICValues(m_ic, XNPreeditAttributes, attr, NULL);
	XFree(attr);
    }
}

void TextView::setPreeditCaret(int pos)
{
    m_preeditCaret = pos;
}

void TextView::setPreeditString(const wchar_t *str, int pos, int length)
{
    if (str == NULL) {
	m_preeditString.erase(pos, length);
    } else {
	if (length > 0) {
	    m_preeditString.replace(pos, length, str);
	} else {
	    m_preeditString.insert(pos, str);
	}
    }
}

void TextView::getSpotLocation(XPoint &point)
{
    std::wstring tmp = m_text[m_caret.y]->substr(0, m_caret.x);

    int width = XwcTextEscapement(m_fontset, tmp.c_str(), tmp.size());
    if (width > 0)
	point.x = width + 1;
    else
	point.x = 0;
    point.y = m_fontsetRect.height * m_caret.y - m_fontsetRect.y;
}

void TextView::onExpose()
{
    draw();
}

void TextView::onFocusIn()
{
    if (m_ic == NULL)
	return;

    XSetICFocus(m_ic);
}

void TextView::onFocusOut()
{
    if (m_ic == NULL)
	return;

    wchar_t *str = XwcResetIC(m_ic);	
    m_preeditString.clear();

    if (str != NULL) {
	insert(str);
	draw();
    }

    XUnsetICFocus(m_ic);
}

void TextView::onKeyPress(XKeyPressedEvent *event)
{
    char buf[256] = { '\0', };
    wchar_t wbuf[256] = { L'\0', };
    KeySym keysym = 0;
    Status status = XLookupNone;

    if (m_ic == NULL) {
	XLookupString(event, buf, sizeof(buf), &keysym, NULL);
	mbstowcs(wbuf, buf, strlen(buf));
	status = XLookupChars;
    } else {
	XwcLookupString(m_ic, event, wbuf, NELEMENTS(wbuf), &keysym, &status);
    }

    if (status == XLookupChars ||
	status == XLookupKeySym ||
	status == XLookupBoth) {
	if (keysym == XK_Return) {
	    onKeyReturn();
	} else if (keysym == XK_Delete) {
	    onKeyDelete();
	} else if (keysym == XK_BackSpace) {
	    onKeyBackspace();
	} else if (keysym == XK_Escape) {
	    onKeyEscape();
	    return;
	} else if (keysym == XK_Left) {
	    moveCaret(-1, 0);
	} else if (keysym == XK_Right) {
	    moveCaret(1, 0);
	} else if (keysym == XK_Up) {
	    moveCaret(0, -1);
	} else if (keysym == XK_Down) {
	    moveCaret(0, 1);
	} else if (keysym == XK_Home) {
	    onKeyHome();
	} else if (keysym == XK_End) {
	    onKeyEnd();
	} else {
	    if ((status == XLookupChars || status == XLookupBoth) &&
		((event->state & ControlMask) != ControlMask) &&
		((event->state & Mod1Mask) != Mod1Mask))
		insert(wbuf);
	}
	updateSpotLocation();
	draw();
    }
}

void TextView::onKeyReturn()
{
    if (m_caret.x < (int)m_text[m_caret.y]->size()) {
	std::wstring last = m_text[m_caret.y]->substr(m_caret.x,
					m_text[m_caret.y]->size() - m_caret.x);
	m_text[m_caret.y]->erase(m_caret.x, last.size());
	m_text.insert(m_text.begin() + m_caret.y + 1, new std::wstring(last));
    } else {
	m_text.insert(m_text.begin() + m_caret.y + 1, new std::wstring(L""));
    }

    // move the cursor
    m_caret.x = 0;
    m_caret.y++;
}

void TextView::onKeyDelete()
{
    if (m_caret.x < (int)m_text[m_caret.y]->size()) {
	m_text[m_caret.y]->erase(m_caret.x, 1);
    } else {
	if (m_caret.y < (int)m_text.size() - 1) {
	    if (m_text[m_caret.y + 1]->size() > 0) {
		m_text[m_caret.y]->append(*m_text[m_caret.y + 1]);
	    }

	    delete m_text[m_caret.y + 1];
	    m_text.erase(m_text.begin() + m_caret.y + 1);
	}
    }
}

void TextView::onKeyBackspace()
{
    if (m_caret.x > 0) {
	m_caret.x--;
	m_text[m_caret.y]->erase(m_caret.x, 1);
    } else {
	if (m_caret.y > 0) {
	    m_caret.x = m_text[m_caret.y - 1]->size();
	    m_caret.y--;

	    if (m_text[m_caret.y + 1]->size() > 0) {
		m_text[m_caret.y]->append(*m_text[m_caret.y + 1]);
	    }

	    delete m_text[m_caret.y + 1];
	    m_text.erase(m_text.begin() + m_caret.y + 1);
	}
    }
}

void TextView::onKeyHome()
{
    m_caret.x = 0;
}

void TextView::onKeyEnd()
{
    m_caret.x = m_text[m_caret.y]->size();
}

void TextView::onKeyEscape()
{
    destroy();
}

void TextView::onClientMessage(const XClientMessageEvent *event)
{
    if(event->data.l[0] == (long)m_WMDeleteWindow){
	destroy();
    }
}

void TextView::IMInstantiateCallback(Display *display,
				     XPointer client_data, XPointer data)
{
    if (client_data == NULL)
	return;

    printf("XIM is available now\n");
    TextView *textview = reinterpret_cast<TextView*>(client_data);
    textview->openIM();
}

void TextView::IMDestroyCallback(XIM im, XPointer client_data, XPointer data)
{
    printf("xim is destroyed\n");

    if (client_data == NULL)
	return;

    TextView *textview = reinterpret_cast<TextView*>(client_data);

    textview->m_im = NULL;
    textview->m_ic = NULL;
    textview->registerInstantiateCallback();
}

void TextView::preeditStartCallback(XIM xim, XPointer user_data, XPointer data)
{
    printf("preedit start\n");
}

void TextView::preeditDoneCallback(XIM xim, XPointer user_data, XPointer data)
{
    printf("preedit done\n");

    if (user_data == NULL)
	return;

    TextView *textview = reinterpret_cast<TextView*>(user_data);
    textview->setPreeditString(NULL, 0, 0);
}

void TextView::preeditDrawCallback(XIM xim, XPointer user_data, XPointer data)
{
    if (user_data == NULL || data == NULL)
	return;

    TextView *textview = reinterpret_cast<TextView*>(user_data);
    XIMPreeditDrawCallbackStruct *draw_data = 
	    reinterpret_cast<XIMPreeditDrawCallbackStruct*>(data);

    textview->setPreeditCaret(draw_data->caret);

    if (draw_data->text == NULL) {
	textview->setPreeditString(NULL,
		draw_data->chg_first, draw_data->chg_length);
    } else {
	if (draw_data->text->encoding_is_wchar) {
	    textview->setPreeditString(draw_data->text->string.wide_char,
				draw_data->chg_first, draw_data->chg_length);
	} else {
	    wchar_t str[256] = { L'\0', };
	    mbstowcs(str, draw_data->text->string.multi_byte, sizeof(str));
	    textview->setPreeditString(str,
		    draw_data->chg_first, draw_data->chg_length);
	}
    }
    textview->draw();
}

void TextView::preeditCaretCallback(XIM xim, XPointer user_data, XPointer data)
{
    if (user_data == NULL || data == NULL)
	return;

    TextView *textview = reinterpret_cast<TextView*>(user_data);
    XIMPreeditCaretCallbackStruct *caret_data = 
	    reinterpret_cast<XIMPreeditCaretCallbackStruct*>(data);

    switch (caret_data->direction) {
    case XIMForwardChar:
	textview->moveCaret(1, 0);
	break;
    case XIMBackwardChar:
	textview->moveCaret(-1, 0);
	break;
    case XIMDontChange:
	break;
    default:
	printf("preedit caret: %d\n", caret_data->direction);
	break;
    }
    textview->draw();
}

int
main(int argc, char *argv[])
{
    Display *display = XOpenDisplay("");
    if (display == NULL) {
	printf("Can't open display\n");
	return 0;
    }

    char *locale = setlocale(LC_CTYPE, "");
    if (locale == NULL) {
	printf("Can't set locale\n");
	XCloseDisplay(display);
	return 0;
    }
    printf("locale: %s\n", locale);

    if (!XSupportsLocale()) {
	printf("X does not support locale\n");
	XCloseDisplay(display);
	return 0;
    }

    char *modifiers = XSetLocaleModifiers("");
    if (modifiers == NULL) {
	printf("Can't set locale modifiers\n");
	XCloseDisplay(display);
	return 0;
    }
    printf("modifiers: %s\n", modifiers);

    int inputStyle = XIMPreeditCallbacks;
    char *title = "XIM client - On the spot";
    if (argc >= 2) {
	if (strcmp(argv[1], "-on") == 0) {
	    inputStyle = XIMPreeditCallbacks;
	    title = "XIM client - On the spot";
	} else if (strcmp(argv[1], "-over") == 0) {
	    inputStyle = XIMPreeditPosition;
	    title = "XIM client - over the spot";
	} else if (strcmp(argv[1], "-off") == 0) {
	    inputStyle = XIMPreeditArea;
	    title = "XIM client - off the spot";
	} else if (strcmp(argv[1], "-root") == 0) {
	    inputStyle = XIMPreeditNothing;
	    title = "XIM client - root window";
	}
    }

    TextView textview;
    textview.create(display, 0, inputStyle);
    textview.setTitle(title);

    XEvent event;
    for (;;) {
	XNextEvent(display, &event);
	if (XFilterEvent(&event, None))
	    continue;

	switch(event.type){
	case FocusIn:
	    textview.onFocusIn();
	    break;
	case FocusOut:
	    textview.onFocusOut();
	    break;
	case Expose:
	    textview.onExpose();
	    break;
	case KeyPress:
	    textview.onKeyPress(&event.xkey);
	    break;
	case ClientMessage:
	    textview.onClientMessage(&event.xclient);
	    break;
	default:
	    break;
	}

	if (textview.isDestroyed()) {
	    break;
	}
    }

    XCloseDisplay(display);

    return 0;
}
