#include "PluginEditor.h"

using namespace ts;
using namespace ts::ui;

namespace
{
juce::String J (const char* s) { return juce::String::fromUTF8 (s); }
juce::Font font (float size, bool bold = false) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }
juce::Font mono (float size) { return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), size, juce::Font::plain)); }
float textW (const juce::Font& f, const juce::String& t) { return juce::GlyphArrangement::getStringWidth (f, t); }
juce::String num (double v, int d) { auto s = juce::String (v, d); if (s.containsChar ('.')) { s = s.trimCharactersAtEnd ("0"); s = s.trimCharactersAtEnd ("."); } return s; }
juce::String fmtT (double ms)
{
    if (ms < 1) return juce::String ((int) std::lround (ms * 1000)) + J (" µs");
    if (ms < 1000) return num (ms, ms < 10 ? 1 : 0) + " ms";
    return num (ms / 1000, ms < 10000 ? 1 : 0) + " s";
}
// precise time (trailing zeros dropped): 12.345 ms, 437.5 ms, 1.2345 s
juce::String fmtP (double ms) { return ms < 1000 ? num (ms, ms < 100 ? 3 : 2) + " ms" : num (ms / 1000, 4) + " s"; }
juce::String fmtMs (double ms) { return ms >= 1000 ? num (ms / 1000, 4) + J (" s（") + num (ms, 2) + J (" ms）") : num (ms, 3) + " ms"; }
juce::String noteName (int i) { return i <= 0 ? juce::String ("FREE") : juce::String (ts::notes()[(size_t) juce::jlimit (1, ts::numNotes() - 1, i)].name); }
juce::String hz (double f) { return f >= 1000 ? num (f / 1000, f >= 10000 ? 1 : 2) + " kHz" : juce::String ((int) std::lround (f)) + " Hz"; }
juce::String sdb (double d) { return (d > 0 ? "+" : "") + juce::String (d, 1) + " dB"; }
juce::String pct (double v) { return juce::String ((int) std::lround (v)) + " %"; }
juce::String offOr (double v) { return v > 0 ? juce::String ((int) std::lround (v)) : juce::String ("OFF"); }
juce::String cutLo (double v) { return v < 20 ? juce::String ("OFF") : hz (v); }
juce::String cutHi (double v) { return v >= 20000 ? juce::String ("OFF") : hz (v); }
constexpr int kW = 1240, kHeader = 48, kMap = 214, kHint = 26, kGap = 8, kMargin = 12, kToolbar = 34;

const char* subtitleOf (int t)
{
    static const char* s[] = {
        "録音 EQ → テープ飽和（2× OS）→ 再生 EQ・ヘッドバンプ → ワウ・フラッター → ヒス",
        "デジタル HQ・テープエコー・アナログ BBD・ヴィンテージ・デジタル。ピンポン・リバース・ウェット EQ",
        "アルゴリズム（16 ライン FDN）／IR 畳み込み（遅延ゼロ）／リバース・リバーブ" };
    return s[clampv (t, 0, 2)];
}
juce::StringArray variantNames (int t)
{
    switch (t)
    {
        case kTape: return { "Studio 2-Track", "Multitrack", "Cassette" };
        case kDelay: return { "Digital HQ", "Tape Echo", "Analog BBD", "Vintage Digital" };
        default: return { "Hall", "Room", "Plate", "Chamber", "Cathedral" };
    }
}
const char* variantDesc (int t, int v)
{
    static const char* tape[] = { "Studio 2-Track：1/4 インチ 2 トラックのマスターレコーダー。素直でワイドレンジ", "Multitrack：2 インチ多チャンネル機。ヘッドバンプが大きく、低域が太い", "Cassette：1-7/8 ips のカセットデッキ。高域が早めに丸まり、ヒスとワウが多い" };
    static const char* dl[] = { "Digital HQ：64 bit 浮動小数点・補間付きのクリーンなデジタルディレイ。時間変更はクロスフェード（ピッチが揺れない）", "Tape Echo：テープループのエコーマシン。3 つの再生ヘッド、繰り返すほど丸く歪み、時間を変えるとピッチが滑る", "Analog BBD：バケツリレー素子のアナログディレイ。時間を長くするほど帯域が狭く暗くなる", "Vintage Digital：初期のデジタルラック。12 bit 量子化と 9 kHz の帯域制限" };
    static const char* rv[] = { "Hall：中〜大ホール。なめらかで広い", "Room：小さめの部屋。初期反射が多く短い", "Plate：鉄板リバーブ。初期反射なしで密度がすぐ高い、明るい", "Chamber：エコーチェンバー。中くらいの密度で温かい", "Cathedral：大聖堂。長いプリディレイと巨大な空間" };
    if (t == kTape) return tape[clampv (v, 0, 2)];
    if (t == kDelay) return dl[clampv (v, 0, 3)];
    return rv[clampv (v, 0, 4)];
}
const char* compDesc (int k, int i)
{
    static const char* d[3][3] = {
        { "真空管の録音／再生アンプ。早めに飽和し、偶数倍音が多い", "ソリッドステートの電子回路。素直で余裕がある", "トランスレスの現代的な回路。テープ以外の色付けはほぼなし" },
        { "新品のヘッド", "摩耗したヘッド。高域が下がり、ヘッドバンプが少し大きい", "研磨し直したヘッド。わずかに明るい" },
        { "標準の録音レベル（0 VU = −18 dBFS）", "+3 dB 高めの録音レベル。同じ入力でより飽和する", "+6 dB のホットな録音。テープを強く押す" } };
    return d[clampv (k, 0, 2)][clampv (i, 0, 2)];
}
const char* irDesc (int i)
{
    static const char* d[] = { "Concert Hall：3.4 秒の大ホール。低域が長く残る", "Wood Room：木の部屋。短く温かい", "Bright Plate：明るい鉄板。初期反射なし", "Stone Chamber：石造りのチェンバー",
                               "Vocal Booth：狭いブース。ごく短い響き", "Large Church：7 秒の大きな教会", "Spring Tank：2 本のスプリング。“ボヨン”としたチャープ" };
    return d[clampv (i, 0, 6)];
}
} // namespace

juce::Colour ts::ui::hlColour (int h)
{
    switch (h)
    {
        case hlTape: return col::tape; case hlMotion: return col::motion; case hlTime: return col::time; case hlDelay: return col::delay;
        case hlEq: return col::eq; case hlRev: return col::rev; case hlMix: return col::mix; case hlAnalog: return col::analog; default: return col::inkDim;
    }
}

// ============================================================================ Look
Look::Look()
{
    setColour (juce::ComboBox::backgroundColourId, col::panel2);
    setColour (juce::ComboBox::outlineColourId, col::line);
    setColour (juce::ComboBox::textColourId, col::ink);
    setColour (juce::ComboBox::arrowColourId, col::inkDim);
    setColour (juce::PopupMenu::backgroundColourId, col::panel2);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colour (0xff1a1408));
    setColour (juce::TextButton::buttonColourId, col::panel2);
    setColour (juce::TextButton::textColourOffId, col::ink);
    setColour (juce::TextButton::textColourOnId, juce::Colour (0xff1a1408));
}
void Look::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s)
{
    const auto c = s.findColour (juce::Slider::rotarySliderFillColourId);
    auto b = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2);
    const float d = std::min (b.getWidth(), b.getHeight()), r = d * 0.5f - 2, cx = b.getCentreX(), cy = b.getCentreY();
    g.setColour (juce::Colour (0xff2d323b)); g.fillEllipse (cx - r * 0.72f, cy - r * 0.72f, r * 1.44f, r * 1.44f);
    g.setColour (juce::Colour (0xff111317)); g.drawEllipse (cx - r * 0.72f, cy - r * 0.72f, r * 1.44f, r * 1.44f, 2.f);
    if ((bool) s.getProperties()["stepless"])
    {
        juce::Path rim; rim.addEllipse (cx - r * 0.72f, cy - r * 0.72f, r * 1.44f, r * 1.44f);
        juce::Path dd; const float pat[] = { 2.f, 3.f }; juce::PathStrokeType (1.6f).createDashedStroke (dd, rim, pat, 2);
        g.setColour (col::accent); g.fillPath (dd);
    }
    juce::Path tr; tr.addCentredArc (cx, cy, r, r, 0, a0, a1, true);
    g.setColour (juce::Colour (0xff3a404b)); g.strokePath (tr, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const bool bip = (bool) s.getProperties()["bipolar"];
    const float av = a0 + pos * (a1 - a0), from = bip ? (float) (a0 + (a1 - a0) * (double) s.getProperties()["zeroPos"]) : a0;
    if (std::abs (av - from) > 0.01f && s.isEnabled())
    {
        juce::Path va; va.addCentredArc (cx, cy, r, r, 0, std::min (from, av), std::max (from, av), true);
        g.setColour (c); g.strokePath (va, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    juce::Path p; p.addRoundedRectangle (-1.3f, -r * 0.72f, 2.6f, r * 0.55f, 1.3f);
    g.setColour (juce::Colour (0xfff2f3f5)); g.fillPath (p, juce::AffineTransform::rotation (av).translated (cx, cy));
}
void Look::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = b.getToggleState(), round = b.getProperties()["round"];
    const auto onCol = (bool) b.getProperties()["analog"] ? col::analog : col::accent;
    const float rad = round ? r.getHeight() * 0.5f : 4.f;
    g.setColour (on ? onCol : col::panel2); g.fillRoundedRectangle (r, rad);
    g.setColour (on ? onCol : (over ? col::inkDim : col::line)); g.drawRoundedRectangle (r, rad, 1.f);
}
void Look::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? juce::Colour (0xff1a1408) : (b.isEnabled() ? col::ink : col::inkDim.withAlpha (0.4f)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}
juce::Font Look::getTextButtonFont (juce::TextButton&, int h) { return font (std::min (13.f, h * 0.55f), true); }

// ============================================================================ VKnob
VKnob::VKnob (const juce::String& cap, int sl, int hl, HoverSink& s) : slot (sl), band (hl), caption (cap), sink (s)
{
    slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::degreesToRadians (225.f), juce::degreesToRadians (495.f), true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, hl == hlNone ? juce::Colour (0xffc9ced8) : hlColour (hl));
    slider.setMouseDragSensitivity (180);
    slider.setVelocityModeParameters (1.0, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
    slider.addMouseListener (this, false);
    slider.onValueChange = [this]
    {
        if (! syncing && set) set (slider.getValue());
        repaint();
        if (slider.isMouseOverOrDragging()) sink.hover (slot, band, describe ? describe() : caption + ": " + (text ? text() : juce::String (slider.getValue())));
    };
    slider.onDragStart = [] {};
    addAndMakeVisible (slider);
}
void VKnob::setList (int n) { slider.setRange (0, std::max (1, n - 1), 1); }
void VKnob::setLin (double lo, double hi, double step)
{
    slider.setRange (lo, hi, step);
    if (lo < 0 && hi > 0) { slider.getProperties().set ("bipolar", true); slider.getProperties().set ("zeroPos", -lo / (hi - lo)); }
}
void VKnob::setLog (double lo, double hi) { slider.setRange (std::log (lo), std::log (hi), 0); }
void VKnob::sync()
{
    if (! get || slider.isMouseButtonDown()) { repaint(); return; }
    const juce::ScopedValueSetter<bool> sv (syncing, true);
    slider.setValue (get(), juce::dontSendNotification);
    slider.setDoubleClickReturnValue (true, def);
    repaint();
}
void VKnob::resized() { slider.setBounds (getLocalBounds().withHeight (54).withSizeKeepingCentre (54, 54).withY (0)); }
void VKnob::paint (juce::Graphics& g)
{
    const float a = isEnabled() ? 1.f : 0.3f;
    g.setColour (col::inkDim.withAlpha (a)); g.setFont (font (12.f, true));
    g.drawText (caption.toUpperCase(), 0, 54, getWidth(), 15, juce::Justification::centred);
    g.setColour (col::ink.withAlpha (a)); g.setFont (mono (11.5f));
    g.drawText (text ? text() : juce::String (slider.getValue(), 1), 0, 69, getWidth(), 14, juce::Justification::centred);
}
void VKnob::mouseEnter (const juce::MouseEvent&) { sink.hover (slot, band, describe ? describe() : caption); }
void VKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.eventComponent != this || e.y < 66 || ! isEnabled()) return;
    if (onValueClick) { onValueClick(); return; }
    if (! plainSet || ! plainGet) return;
    entry = std::make_unique<juce::TextEditor>();
    entry->setFont (mono (11.5f)); entry->setJustification (juce::Justification::centred);
    entry->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff111317));
    entry->setColour (juce::TextEditor::outlineColourId, col::accent); entry->setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
    entry->setColour (juce::TextEditor::textColourId, col::ink);
    entry->setText (juce::String (plainGet(), 4).trimCharactersAtEnd ("0").trimCharactersAtEnd ("."), false);
    entry->setBounds (0, 67, getWidth(), 18);
    entry->onReturnKey = [this] { finishEntry (true); };
    entry->onEscapeKey = [this] { finishEntry (false); };
    entry->onFocusLost = [this] { finishEntry (true); };
    addAndMakeVisible (*entry); entry->selectAll(); entry->grabKeyboardFocus();
}
void VKnob::finishEntry (bool apply)
{
    if (! entry) return;
    const auto t = entry->getText().trim().toLowerCase().replaceCharacter (',', '.');
    if (apply && t.containsAnyOf ("0123456789"))
    {
        double v = t.getDoubleValue();
        if (msUnit && t.endsWithChar ('s') && ! t.endsWith ("ms")) v *= 1000;          // "1.25 s" → 1250 ms
        plainSet (juce::jlimit (plainLo, plainHi, v));
    }
    juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<VKnob> (this)] { if (auto* k = sp.getComponent()) { k->entry.reset(); k->repaint(); } });
}

// ============================================================================ SyncPicker
SyncPicker::SyncPicker()
{
    setWantsKeyboardFocus (true);
    search.setTextToShowWhenEmpty (J ("Search（例：7/32, dot, trip）"), col::inkDim);
    search.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1b1c1e));
    search.setColour (juce::TextEditor::outlineColourId, juce::Colour (0xff45484d));
    search.setColour (juce::TextEditor::textColourId, col::ink);
    search.setFont (mono (13.f));
    search.onTextChange = [this] { filter(); };
    search.onReturnKey = [this] { pick (cur); };
    search.onEscapeKey = [this] { close(); };
    addAndMakeVisible (search);
    const std::pair<const char*, int> qj[] = { { "1/64", 1 }, { "1/8", 10 }, { "1 BAR", 19 }, { "n/64", 27 }, { "\xe2\x97\x8e", -1 } };
    for (auto& q : qj)
    {
        auto* b = jumps.add (new juce::TextButton (juce::String::fromUTF8 (q.first)));
        const int to = q.second;
        b->onClick = [this, to] { mark (to < 0 && getter ? getter() : std::max (0, to)); search.grabKeyboardFocus(); };
        addAndMakeVisible (b);
    }
    closeBtn.onClick = [this] { close(); };
    addAndMakeVisible (closeBtn);
    match.assign ((size_t) numNotes(), true);
}
void SyncPicker::open (const juce::String& t, std::function<int()> g, std::function<void (int)> s, std::function<double()> b)
{
    title = t; getter = std::move (g); setter = std::move (s); bpmFn = std::move (b);
    search.setText ({}, false); match.assign ((size_t) numNotes(), true);
    cur = getter ? getter() : 0;
    setVisible (true); toFront (true);
    search.grabKeyboardFocus();
    repaint();
}
void SyncPicker::close() { setter = nullptr; setVisible (false); }
void SyncPicker::resized()
{
    box = getLocalBounds().reduced (16).withSizeKeepingCentre (std::min (1180, getWidth() - 32), std::min (640, getHeight() - 32));
    auto b = box;
    head = b.removeFromTop (50); foot = b.removeFromBottom (36); grid = b.reduced (16, 10);
    auto h = head.reduced (12, 10);
    closeBtn.setBounds (h.removeFromRight (34)); h.removeFromRight (8);
    search.setBounds (h.removeFromRight (260)); h.removeFromRight (8);
    h.removeFromLeft (190 + 96);
    for (auto* j : jumps) { j->setBounds (h.removeFromLeft (j->getButtonText().length() > 3 ? 58 : 48)); h.removeFromLeft (6); }
}
juce::Rectangle<int> SyncPicker::cell (int i) const
{
    const int col = i / kRows, row = i % kRows, cw = grid.getWidth() / 4, rh = grid.getHeight() / kRows;
    return { grid.getX() + col * cw, grid.getY() + row * rh, cw - 14, rh };
}
int SyncPicker::hit (juce::Point<int> p) const { for (int i = 0; i < numNotes(); ++i) if (cell (i).contains (p)) return i; return -1; }
void SyncPicker::mark (int i) { cur = juce::jlimit (0, numNotes() - 1, i); repaint(); }
void SyncPicker::pick (int i)
{
    auto f = setter; close();
    if (f && i >= 0) f (i);
}
void SyncPicker::filter()
{
    const auto q = search.getText().trim().toLowerCase();
    int first = -1;
    for (int i = 0; i < numNotes(); ++i)
    {
        const auto name = juce::String (notes()[(size_t) i].name).toLowerCase();
        bool h = q.isEmpty() || juce::String (i + 1) == q || juce::String (i + 1).paddedLeft ('0', 2) == q;
        for (int k = name.indexOf (q); ! h && k >= 0; k = name.indexOf (k + 1, q)) if (k == 0 || name[k - 1] == ' ' || name[k - 1] == '(') h = true;
        match[(size_t) i] = h; if (h && first < 0) first = i;
    }
    if (first >= 0) cur = first;
    repaint();
}
void SyncPicker::mouseMove (const juce::MouseEvent& e) { const int i = hit (e.getPosition()); if (i >= 0 && i != cur) mark (i); }
void SyncPicker::mouseDown (const juce::MouseEvent& e)
{
    if (! box.contains (e.getPosition())) { close(); return; }
    const int i = hit (e.getPosition()); if (i >= 0) pick (i);
}
bool SyncPicker::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey) { close(); return true; }
    if (k == juce::KeyPress::returnKey) { pick (cur); return true; }
    if (k == juce::KeyPress::downKey) { mark (cur + 1); return true; }
    if (k == juce::KeyPress::upKey) { mark (cur - 1); return true; }
    if (k == juce::KeyPress::rightKey) { mark (cur + kRows); return true; }
    if (k == juce::KeyPress::leftKey) { mark (cur - kRows); return true; }
    return false;
}
void SyncPicker::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0x8c08090b));
    g.setColour (juce::Colour (0xff1b1c1e)); g.fillRoundedRectangle (box.toFloat(), 8.f);
    g.setColour (juce::Colour (0xff262729)); g.fillRect (head); g.fillRect (foot);
    g.setColour (juce::Colour (0xff3a3d42)); g.drawRoundedRectangle (box.toFloat(), 8.f, 1.f);
    g.setColour (col::ink); g.setFont (font (15.f, true)); g.drawText (title, head.getX() + 14, head.getY(), 190, head.getHeight(), juce::Justification::centredLeft);
    g.setColour (col::inkDim); g.setFont (font (12.f, true)); g.drawText ("QUICK JUMP", head.getX() + 14 + 190, head.getY(), 92, head.getHeight(), juce::Justification::centredLeft);
    const int sel = getter ? getter() : -1;
    for (int i = 0; i < numNotes(); ++i)
    {
        auto r = cell (i);
        const bool isSel = i == sel, isCur = i == cur;
        if (isSel) { g.setColour (juce::Colour (0xff13313a)); g.fillRect (r); g.setColour (juce::Colour (0xff43d3e6)); g.fillRect (r.withWidth (3)); }
        else if (isCur) { g.setColour (juce::Colour (0xff24282e)); g.fillRect (r); }
        if (isCur && isSel) { g.setColour (juce::Colour (0xff6fe3f0).withAlpha (0.25f)); g.drawRect (r); }
        const float a = match[(size_t) i] ? 1.f : 0.18f;
        g.setFont (mono (13.f));
        g.setColour (juce::Colour (0xff7d8492).withAlpha (a)); g.drawText (juce::String (i + 1).paddedLeft ('0', 2), r.getX() + 10, r.getY(), 28, r.getHeight(), juce::Justification::centredLeft);
        g.setColour ((isSel ? juce::Colour (0xff6fe3f0) : col::ink).withAlpha (a));
        g.drawText (juce::String (notes()[(size_t) i].name), r.getX() + 40, r.getY(), r.getWidth() - 44, r.getHeight(), juce::Justification::centredLeft);
    }
    const double ms = noteMs (cur, bpmFn ? bpmFn() : 120);
    g.setFont (mono (13.f));
    g.setColour (juce::Colour (0xff7d8492)); g.drawText (juce::String (cur + 1).paddedLeft ('0', 2), foot.getX() + 16, foot.getY(), 30, foot.getHeight(), juce::Justification::centredLeft);
    g.setColour (col::ink); g.drawText (cur == 0 ? J ("None（FREE）") : juce::String (notes()[(size_t) cur].name), foot.getX() + 50, foot.getY(), 300, foot.getHeight(), juce::Justification::centredLeft);
    g.setColour (col::inkDim);
    g.drawText (ms < 0 ? J ("テンポ同期なし（ミリ秒で設定）") : (ms >= 1000 ? num (ms / 1000, 4) + " s (" + num (ms, 2) + " ms)" : num (ms, 3) + " ms") + " @ " + num (bpmFn ? bpmFn() : 120, 2) + " BPM",
                foot.withTrimmedRight (16), juce::Justification::centredRight);
}
void VKnob::mouseExit (const juce::MouseEvent&) { if (! slider.isMouseButtonDown()) sink.hover (-1, hlNone, {}); }

// ============================================================================ Seg
Seg::Seg (const juce::String& cap, const juce::StringArray& names, int sl, int hl, HoverSink& s) : caption (cap), slot (sl), band (hl), sink (s)
{
    for (int i = 0; i < names.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (names[i]));
        b->onClick = [this, i] { if (set) set (i); sync(); if (describe) sink.hover (slot, band, describe (i)); };
        b->onStateChange = [this, i, b] { if (b->isOver() && describe) sink.hover (slot, band, describe (i)); };
        addAndMakeVisible (b);
    }
}
void Seg::sync() { const int v = get ? get() : -1; for (int i = 0; i < buttons.size(); ++i) buttons[i]->setToggleState (i == v, juce::dontSendNotification); }
int Seg::preferredWidth() const
{
    int w = 0; for (auto* b : buttons) w += std::max (40, (int) textW (font (12.5f, true), b->getButtonText()) + 18) + 3;
    return std::max (w, caption.isEmpty() ? 0 : (int) textW (font (12.f, true), caption) + 8);
}
void Seg::resized()
{
    int x = 0; const int y = caption.isEmpty() ? 10 : 22;
    for (auto* b : buttons) { const int w = std::max (40, (int) textW (font (12.5f, true), b->getButtonText()) + 18); b->setBounds (x, y, w, 28); x += w + 3; }
}
void Seg::paint (juce::Graphics& g)
{
    if (caption.isEmpty()) return;
    g.setColour (col::inkDim); g.setFont (font (12.f, true));
    g.drawText (caption.toUpperCase(), 0, 2, getWidth(), 16, juce::Justification::left);
}


// ============================================================================ IRBox
void IRBox::resized()
{
    box.setBounds (0, 22, getWidth(), 26);
    button.setBounds (0, 52, getWidth(), 26);
}
void IRBox::paint (juce::Graphics& g)
{
    g.setColour (col::inkDim); g.setFont (font (12.f, true));
    g.drawText (caption.toUpperCase(), 0, 2, getWidth(), 16, juce::Justification::left);
    if (text.isNotEmpty() && ! box.isVisible())
    {
        g.setColour (col::ink); g.setFont (mono (11.5f));
        g.drawFittedText (text, 0, 22, getWidth(), 26, juce::Justification::centredLeft, 1, 0.8f);
    }
}

// ============================================================================ previews
namespace
{
const std::vector<double>& tapeLevels() { static const std::vector<double> v = [] { std::vector<double> l; for (int i = 0; i < 15; ++i) l.push_back (-36.0 + 3 * i); return l; }(); return v; }
const std::vector<double>& respFreqs() { static const std::vector<double> v = [] { std::vector<double> f; for (int i = 0; i < 22; ++i) f.push_back (20.0 * std::pow (1000.0, i / 21.0)); return f; }(); return v; }
void frame (juce::Graphics& g, juce::Rectangle<float> b, bool lit, juce::Colour c, const juce::String& cap, const juce::String& info)
{
    g.setColour (col::panel2); g.fillRoundedRectangle (b, 6.f);
    if (lit) { g.setColour (c); g.drawRoundedRectangle (b.reduced (1), 6.f, 1.5f); }
    g.setColour (col::inkDim); g.setFont (font (11.f, true)); g.drawText (cap, 8, 3, (int) b.getWidth() - 16, 14, juce::Justification::left);
    g.setColour (col::ink); g.setFont (mono (10.f)); g.drawText (info, 8, 3, (int) b.getWidth() - 16, 14, juce::Justification::right);
}
}

void View::refresh (float liveIn)
{
    live = liveIn;
    const Params p = proc.reader.params (slot);
    const double bpm = proc.hostBpm.load();
    const int serial = proc.irSerial (slot);
    if (! valid || p != last || (kind != tapeTransfer && kind != tapeResp && bpm != lastBpm) || ((kind == revEnv || kind == revRT) && serial != lastSerial)) dirty = true;
    const auto now = juce::Time::getMillisecondCounter();
    if (dirty && now - lastCalc > 120)
    {
        dirty = false; lastCalc = now; last = p; lastBpm = bpm; lastSerial = serial; valid = true;
        a.clear(); b.clear(); c.clear(); wave.clear();
        usesIR = ival (p.source) > 0 || p.reverse != 0;
        switch (kind)
        {
            case tapeTransfer: a = tapeCurve (p, 48000, tapeLevels()); break;
            case tapeResp:
            {
                a = tapeLevelResponse (p, 48000, respFreqs(), -28);
                b = tapeLevelResponse (p, 48000, respFreqs(), -6);
                size_t k1 = 0; while (k1 < respFreqs().size() && respFreqs()[k1] <= 900) ++k1;
                extra = b[k1]; for (auto& v : b) v -= extra;
                break;
            }
            case echo:
            {
                ms = delayMs (p, bpm);
                dur = std::min (8000.0, std::max (1500.0, ms * (p.feedback > 60 ? 12 : 7) + (p.reverse != 0 ? ms * 2 : 0)));
                auto pat = echoPattern (p, bpm, dur, dur / 330, 24000);
                a = pat[0]; b = pat[1];
                break;
            }
            case revEnv:
            {
                const Params p = resolveTimes (last, bpm);
                if (! usesIR)
                {
                    dur = std::min (12000.0, std::max (1200.0, p.decay * 1300 + p.predelay));
                    a = reverbEnvelope (p, nullptr, dur, dur / 330, 24000, bpm);
                }
                else
                {
                    const IR ir = proc.currentIR (slot);
                    const double fs = proc.getHostRate();
                    if (! ir[0].empty())
                    {
                        const int n = (int) ir[0].size(), pre = (int) std::lround (p.predelay * fs / 1000), bins = 330;
                        dur = (n + pre) / fs * 1000;
                        const double step = (double) (n + pre) / bins;
                        for (int bi = 0; bi < bins; ++bi)
                        {
                            double e = 0; int cnt = 0;
                            for (int i = (int) std::floor (bi * step) - pre; i < (int) std::floor ((bi + 1) * step) - pre; ++i, ++cnt)
                                if (i >= 0 && i < n) e += (double) ir[0][(size_t) i] * ir[0][(size_t) i] + (double) ir[1][(size_t) i] * ir[1][(size_t) i];
                            a.push_back (10 * std::log10 (e / std::max (1, 2 * cnt) + 1e-14));
                        }
                    }
                }
                break;
            }
            case revRT:
            {
                if (ival (p.source) == 0) a = rtCurve (resolveTimes (p, bpm), 48000, respFreqs());
                else
                {
                    const IR ir = proc.currentIR (slot);
                    const int n = (int) ir[0].size();
                    if (n > 0) { const double ws = n / 150.0; for (int bi = 0; bi < 150; ++bi) { float hi = 0, lo = 0; for (int i = (int) (bi * ws); i < (int) ((bi + 1) * ws) && i < n; ++i) { hi = std::max (hi, ir[0][(size_t) i]); lo = std::min (lo, ir[0][(size_t) i]); } wave.push_back ({ hi, lo }); } }
                }
                break;
            }
        }
    }
    repaint();
}
void View::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    auto r = bounds.reduced (8).withTrimmedTop (14);
    const auto& P = last;
    switch (kind)
    {
        case tapeTransfer:
        {
            const double lo = -36, hi = 6;
            const double lv = live > 0 ? linToDb (live) : -200;
            juce::String info = J ("0 VU=−18");
            auto X = [&] (double d) { return r.getX() + (float) ((d - lo) / (hi - lo)) * r.getWidth(); };
            auto Y = [&] (double d) { return r.getBottom() - (float) ((juce::jlimit (lo - 6, hi, d) - lo) / (hi - lo)) * r.getHeight(); };
            double y = 0;
            if (lv > lo && a.size() == 15) { const int k = juce::jlimit (0, 13, (int) std::floor ((lv - lo) / 3)); const double t = (lv - (lo + 3 * k)) / 3; y = a[(size_t) k] + t * (a[(size_t) k + 1] - a[(size_t) k]); info = juce::String ((int) std::lround (lv)) + ">" + juce::String ((int) std::lround (y)); }
            frame (g, bounds, lit, col::tape, "IN > OUT 1 kHz", info);
            g.setColour (col::line);
            for (double v : { -30.0, -18.0, -6.0 }) { g.drawVerticalLine ((int) X (v), r.getY(), r.getBottom()); g.drawHorizontalLine ((int) Y (v), r.getX(), r.getRight()); }
            const float dl[] = { 3, 3 }; g.drawDashedLine ({ X (lo), Y (lo), X (hi), Y (hi) }, dl, 2, 1.f);
            if (a.size() != 15) return;
            juce::Path p; const auto& L = tapeLevels();
            for (size_t i = 0; i < L.size(); ++i) i ? p.lineTo (X (L[i]), Y (a[i])) : p.startNewSubPath (X (L[i]), Y (a[i]));
            g.setColour (col::tape); g.strokePath (p, juce::PathStrokeType (2.f));
            if (lv > lo) { g.setColour (juce::Colours::white); g.fillEllipse (X (lv) - 3.5f, Y (y) - 3.5f, 7, 7); }
            break;
        }
        case tapeResp:
        {
            frame (g, bounds, lit, col::tape, J ("周波数特性と入力レベル"), b.empty() ? juce::String() : J ("1 kHz で ") + sdb (extra) + J (" 圧縮"));
            const double lo = -15, hi = 6; const auto& F = respFreqs();
            auto X = [&] (double f) { return r.getX() + (float) (std::log (f / 20) / std::log (1000.0)) * r.getWidth(); };
            auto Y = [&] (double d) { return r.getY() + (float) ((hi - juce::jlimit (lo, hi, d)) / (hi - lo)) * r.getHeight(); };
            g.setColour (col::line);
            for (double f : { 100.0, 1000.0, 10000.0 }) g.drawVerticalLine ((int) X (f), r.getY(), r.getBottom());
            for (double d : { 3.0, 0.0, -3.0, -6.0, -9.0, -12.0 }) g.drawHorizontalLine ((int) Y (d), r.getX(), r.getRight());
            auto plot = [&] (const std::vector<double>& v, juce::Colour c, float w)
            { if (v.size() != F.size()) return; juce::Path p; for (size_t i = 0; i < F.size(); ++i) i ? p.lineTo (X (F[i]), Y (v[i])) : p.startNewSubPath (X (F[i]), Y (v[i])); g.setColour (c); g.strokePath (p, juce::PathStrokeType (w)); };
            plot (a, col::ink, 1.5f); plot (b, col::tape, 2.f);
            g.setFont (mono (9.5f)); g.setColour (col::inkDim); g.drawText (J ("小音量 −28 dBFS"), (int) r.getX() + 2, (int) r.getY() + 1, 140, 12, juce::Justification::left);
            g.setColour (col::tape); g.drawText (J ("大音量 −6 dBFS（1k 基準）"), (int) r.getX() + 2, (int) r.getY() + 13, 180, 12, juce::Justification::left);
            g.setColour (juce::Colour (0xff7d8492));
            for (double f : { 100.0, 1000.0, 10000.0 }) g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : "100", (int) X (f) - 26, (int) r.getBottom() - 12, 24, 12, juce::Justification::right);
            break;
        }
        case echo:
        {
            juce::String info = fmtT (ms);
            if (ival (P.sync) > 0) info << " (" << noteName (ival (P.sync)) << ")";
            static const char* st[] = { "MONO", "STEREO", "PING-PONG", "DUAL" };
            info << " · " << st[clampv (ival (P.stereo), 0, 3)]; if (P.reverse != 0) info << " · REVERSE";
            frame (g, bounds, lit, col::time, J ("エコーの並び L ↑ / R ↓"), info);
            const float mid = r.getCentreY();
            g.setColour (col::line); g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
            const float dl[] = { 2, 4 };
            for (double t = 0; t < dur && ms > 0; t += ms) { const float x = r.getX() + (float) (t / dur) * r.getWidth(); g.drawDashedLine ({ x, r.getY(), x, r.getBottom() }, dl, 2, 1.f); }
            const int n = (int) a.size(); if (n == 0) return;
            const float bw = r.getWidth() / n, hh = r.getHeight() * 0.5f - 2;
            auto h = [&] (double v) { return (float) std::max (0.0, (linToDb (v) + 60) / 60) * hh; };
            g.setColour (col::time);
            for (int i = 0; i < n; ++i)
            {
                const float x = r.getX() + i * bw, ha = h (a[(size_t) i]), hb = h (b[(size_t) i]);
                if (ha > 0) g.fillRect (x, mid - ha, std::max (1.2f, bw), ha);
                if (hb > 0) g.fillRect (x, mid, std::max (1.2f, bw), hb);
            }
            g.setColour (col::ink); g.fillRect (r.getX(), r.getY(), 1.5f, r.getHeight());
            g.setColour (col::inkDim); g.setFont (mono (9.5f)); g.drawText (fmtT (dur), r.toNearestInt(), juce::Justification::bottomRight);
            break;
        }
        case revEnv:
        {
            juce::String info;
            const int n = (int) a.size();
            double mx = -200; int pk = 0; for (int i = 0; i < n; ++i) if (a[(size_t) i] > mx) { mx = a[(size_t) i]; pk = i; }
            if (n > 1)
            {
                if (P.reverse != 0) info = "REVERSE " + fmtT (resolveTimes (P, lastBpm).revLen) + J (" · ピーク ") + fmtT ((double) pk / (n - 1) * dur);
                else { double t60 = -1; for (int i = pk; i < n; ++i) if (a[(size_t) i] < mx - 60) { t60 = (double) i / (n - 1) * dur; break; } info = J ("−60 dB まで ") + (t60 < 0 ? "> " + fmtT (dur) : fmtT (t60)); }
            }
            else if (usesIR) info = J ("IR 準備中…");
            frame (g, bounds, lit, col::rev, J ("残響の減衰"), info);
            if (n < 2) return;
            auto Y = [&] (double d) { return r.getY() + (float) std::min (1.0, (mx - d) / 70) * r.getHeight(); };
            g.setColour (col::line); for (int d = 10; d <= 60; d += 10) g.drawHorizontalLine ((int) Y (mx - d), r.getX(), r.getRight());
            juce::Path p; p.startNewSubPath (r.getX(), r.getBottom());
            for (int i = 0; i < n; ++i) p.lineTo (r.getX() + (float) i / (n - 1) * r.getWidth(), Y (a[(size_t) i]));
            p.lineTo (r.getRight(), r.getBottom()); p.closeSubPath();
            g.setColour (col::rev.withAlpha (0.3f)); g.fillPath (p);
            juce::Path l; for (int i = 0; i < n; ++i) { const float x = r.getX() + (float) i / (n - 1) * r.getWidth(); i ? l.lineTo (x, Y (a[(size_t) i])) : l.startNewSubPath (x, Y (a[(size_t) i])); }
            g.setColour (col::rev); g.strokePath (l, juce::PathStrokeType (2.f));
            const double preMs = resolveTimes (P, lastBpm).predelay;
            if (preMs > 0 && dur > 0) { const float x = r.getX() + (float) (preMs / dur) * r.getWidth(); const float dl[] = { 3, 3 }; g.setColour (col::ink); g.drawDashedLine ({ x, r.getY(), x, r.getBottom() }, dl, 2, 1.f); }
            g.setColour (col::inkDim); g.setFont (mono (9.5f)); g.drawText (fmtT (dur), r.toNearestInt(), juce::Justification::bottomRight);
            break;
        }
        case revRT:
        {
            if (! a.empty())
            {
                frame (g, bounds, lit, col::rev, J ("帯域ごとの残響時間"), {});
                const auto& F = respFreqs(); double top = 1; for (auto v : a) top = std::max (top, v); top *= 1.15;
                auto X = [&] (double f) { return r.getX() + (float) (std::log (f / 20) / std::log (1000.0)) * r.getWidth(); };
                auto Y = [&] (double t) { return r.getBottom() - (float) (t / top) * r.getHeight(); };
                g.setColour (col::line); for (double f : { 100.0, 1000.0, 10000.0 }) g.drawVerticalLine ((int) X (f), r.getY(), r.getBottom());
                for (double k : { 0.25, 0.5, 0.75 }) g.drawHorizontalLine ((int) Y (top * k), r.getX(), r.getRight());
                juce::Path p; for (size_t i = 0; i < F.size() && i < a.size(); ++i) i ? p.lineTo (X (F[i]), Y (a[i])) : p.startNewSubPath (X (F[i]), Y (a[i]));
                g.setColour (col::rev); g.strokePath (p, juce::PathStrokeType (2.f));
                g.setColour (col::inkDim); g.setFont (mono (9.5f)); g.drawText (num (top / 1.15, top / 1.15 >= 10 ? 0 : 1) + " s", r.toNearestInt(), juce::Justification::topLeft);
                g.drawText ("100   1k   10k", r.toNearestInt(), juce::Justification::bottomRight);
            }
            else
            {
                frame (g, bounds, lit, col::rev, J ("IR 波形"), wave.empty() ? J ("準備中") : (P.reverse != 0 ? J ("反転済み") : juce::String()));
                float mx = 1e-6f; for (auto& w : wave) mx = std::max ({ mx, w.first, -w.second });
                const float mid = r.getCentreY(), hh = r.getHeight() * 0.5f, bw = r.getWidth() / std::max<size_t> (1, wave.size());
                g.setColour (col::line); g.drawHorizontalLine ((int) mid, r.getX(), r.getRight());
                g.setColour (col::rev);
                for (size_t i = 0; i < wave.size(); ++i) g.fillRect (r.getX() + i * bw, mid - wave[i].first / mx * hh, std::max (1.f, bw), (wave[i].first - wave[i].second) / mx * hh + 1);
            }
            break;
        }
    }
}
void View::mouseMove (const juce::MouseEvent& e)
{
    if (kind != tapeTransfer || a.size() != 15) return;
    auto r = getLocalBounds().toFloat().reduced (8).withTrimmedTop (14);
    const double x = -36 + (e.position.x - r.getX()) / r.getWidth() * 42;
    const int i = juce::jlimit (0, 14, (int) std::lround ((x + 36) / 3));
    const double lv = -36 + 3 * i, y = a[(size_t) i];
    sink.hover (slot, hlTape, J ("入力 ") + juce::String ((int) lv) + J (" dBFS（") + sdb (lv - REF) + J (" VU）→ 出力 ") + juce::String (y, 1) + J (" dBFS（") + sdb (y - lv) + J ("）"));
}
void HistView::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::panel2); g.fillRoundedRectangle (b, 6.f);
    g.setColour (col::inkDim); g.setFont (font (11.f, true));
    g.drawText (type == kTape ? J ("レベル履歴 5 s（IN / OUT）") : type == kDelay ? J ("エコー履歴 5 s（IN / WET）") : J ("残響履歴 5 s（IN / WET）"), 8, 3, 220, 14, juce::Justification::left);
    auto r = b.reduced (8).withTrimmedTop (16);
    auto Y = [&] (float v) { return r.getBottom() - (float) std::max (0.0, (linToDb (v) + 60) / 60) * r.getHeight(); };
    g.setColour (col::line); for (int d = 12; d <= 48; d += 12) g.drawHorizontalLine ((int) Y ((float) dbToLin (-d)), r.getX(), r.getRight());
    auto line = [&] (const std::array<float, N>& v, juce::Colour c, bool fill)
    {
        juce::Path p;
        for (int k = 0; k < N; ++k) { const float x = r.getX() + (float) k / (N - 1) * r.getWidth(), y = Y (v[(size_t) ((pos + k) % N)]); k ? p.lineTo (x, y) : p.startNewSubPath (x, y); }
        if (fill) { juce::Path f (p); f.lineTo (r.getRight(), r.getBottom()); f.lineTo (r.getX(), r.getBottom()); f.closeSubPath(); g.setColour (c.withAlpha (0.28f)); g.fillPath (f); }
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.4f));
    };
    line (ha, col::inkDim, false);
    if (type == kTape) line (hb, col::tape, false);
    else line (hc, type == kDelay ? col::time : col::rev, true);
    g.setColour (col::inkDim); g.setFont (mono (9.5f)); g.drawText (J ("−48 dB"), r.toNearestInt(), juce::Justification::bottomRight);
}
void HarmDisplay::refresh()
{
    const auto p = proc.reader.params (slot);
    const auto now = juce::Time::getMillisecondCounter();
    if ((valid && p == last) || now - lastCalc < 150) return;
    last = p; valid = true; lastCalc = now;
    h100 = harmonics (kTape, p, 48000, 100, -10);
    h1k = harmonics (kTape, p, 48000, 1000, -10);
    repaint();
}
void HarmDisplay::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::panel2); g.fillRoundedRectangle (b, 6.f);
    g.setFont (mono (10.5f)); g.setColour (col::inkDim);
    g.drawText (J ("倍音（入力 −10 dBFS = +8 VU）"), 8, 4, 220, 14, juce::Justification::left);
    g.setColour (col::accent); g.drawText (J ("偶数"), getWidth() - 74, 4, 30, 14, juce::Justification::right);
    g.setColour (col::time); g.drawText (J ("奇数"), getWidth() - 40, 4, 30, 14, juce::Justification::right);
    const float top = 40, bottom = (float) getHeight() - 18, lo = -80;
    for (int k = 0; k < 2; ++k)
    {
        const auto& h = k ? h1k : h100;
        const float x0 = 10 + (float) k * (getWidth() - 20) * 0.5f, bw = 13, gap = 6;
        g.setColour (col::ink); g.setFont (mono (10.5f));
        g.drawText (juce::String (k ? "1 kHz" : "100 Hz") + "  THD " + juce::String (h.thd, h.thd < 10 ? 2 : 1) + " %", (int) x0, 20, 150, 14, juce::Justification::left);
        for (int i = 0; i < 6; ++i)
        {
            const float x = x0 + (float) i * (bw + gap), t = juce::jlimit (0.f, 1.f, ((float) h.h[i] - lo) / -lo), y = bottom - t * (bottom - top);
            g.setColour (juce::Colour (0xff2c313a)); g.fillRect (x, top, bw, bottom - top);
            g.setColour (i % 2 == 0 ? col::accent : col::time); g.fillRect (x, y, bw, bottom - y);
            g.setColour (col::inkDim); g.setFont (mono (9.f));
            g.drawText ("H" + juce::String (i + 2), (int) x - 4, (int) bottom + 2, (int) bw + 8, 12, juce::Justification::centred);
        }
    }
}

// ============================================================================ ModulePanel
ModulePanel::ModulePanel (::TapeSpaceRackEditor& e, TapeSpaceRackProcessor& p, HoverSink& s, int sl, int ty)
    : slot (sl), type (ty), editor (e), proc (p), sink (s), harm (p, sl)
{
    power.getProperties().set ("round", true);
    power.setClickingTogglesState (true);
    power.onClick = [this] { set (id::on, power.getToggleState() ? 1 : 0); };
    addAndMakeVisible (power);
    for (auto* b : { &up, &down, &dupBtn, &delBtn }) addAndMakeVisible (*b);
    up.onClick = [this] { proc.moveSlot (slot, slot - 1); };
    down.onClick = [this] { proc.moveSlot (slot, slot + 1); };
    dupBtn.onClick = [this] { proc.duplicateSlot (slot); };
    delBtn.onClick = [this] { juce::MessageManager::callAsync ([&pr = proc, s = slot] { pr.removeSlot (s); }); };

    if (type != kReverb)
    {
        analogBtn.getProperties().set ("analog", true); analogBtn.setClickingTogglesState (true);
        analogBtn.onClick = [this] { set ("analog", analogBtn.getToggleState() ? 1 : 0); };
        analogBtn.onStateChange = [this]
        {
            if (! analogBtn.isOver()) return;
            const bool on = get ("analog") > 0.5;
            sink.hover (slot, hlAnalog, type == kTape ? (on ? J ("ANALOG オン：テープとアンプの飽和・ワウ・フラッター・ヒス") : J ("ANALOG オフ：テープの音色（EQ）だけ。飽和・揺れ・ノイズなし"))
                                                      : (on ? J ("ANALOG オン：テープエコー／BBD のループ内飽和・揺れ・ノイズ") : J ("ANALOG オフ：ループ内の飽和・揺れ・ノイズなし（フィルターは残る）")));
        };
        addAndMakeVisible (analogBtn);
    }
    if (type == kTape)
    {
        machineBtn.setClickingTogglesState (true); machineBtn.getProperties().set ("analog", true);
        machineBtn.onClick = [this] { editor.setMachineOpen (slot, machineBtn.getToggleState()); };
        machineBtn.onStateChange = [this] { if (machineBtn.isOver()) sink.hover (slot, hlAnalog, J ("マシン：電子回路（真空管／ソリッドステート）・ヘッド・録音レベル・HEADROOM・AGE を編集。倍音のグラフで飽和の具合が見えます")); };
        addAndMakeVisible (machineBtn);
    }
    const auto vn = variantNames (type);
    for (int i = 0; i < vn.size(); ++i) variant.addItem (vn[i], i + 1);
    variant.onChange = [this]
    {
        const int v = variant.getSelectedItemIndex(); if (v < 0 || v == (int) get ("variant")) return;
        set ("variant", v);
        if (type == kDelay) set ("time", std::min (get ("time"), DELAY_MAX[v]));
        sink.hover (-1, hlNone, J (variantDesc (type, v)));
    };
    addAndMakeVisible (variant);
    presetBox.setTextWhenNothingSelected (J ("プリセット…"));
    for (int i = 0; i < p.presets.numModule (ty); ++i) presetBox.addItem (p.presets.moduleName (ty, i), i + 1);
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex(); if (idx < 0) return;
        const auto name = presetBox.getText();
        presetBox.setSelectedId (0, juce::dontSendNotification);
        juce::MessageManager::callAsync ([&pr = proc, &sk = sink, s = slot, idx, name]
        {
            pr.applyModulePreset (s, idx);
            sk.hover (-1, hlNone, juce::String (s + 1) + J (" 番にプリセット「") + name + J ("」を読み込みました"));
        });
    };
    addAndMakeVisible (presetBox);

    if (type == kTape) { views.push_back (std::make_unique<View> (p, sl, View::tapeTransfer, s)); views.push_back (std::make_unique<View> (p, sl, View::tapeResp, s)); }
    else if (type == kDelay) views.push_back (std::make_unique<View> (p, sl, View::echo, s));
    else { views.push_back (std::make_unique<View> (p, sl, View::revEnv, s)); views.push_back (std::make_unique<View> (p, sl, View::revRT, s)); }
    for (auto& v : views) addAndMakeVisible (*v);
    hist.type = type; addAndMakeVisible (hist);

    if (type == kTape)
    {
        static const char* ck[3] = { "c1", "c2", "c3" };
        for (size_t k = 0; k < 3; ++k)
        {
            for (int i = 0; i < 3; ++i) cBox[k].addItem (k == 0 ? ELECTRONICS[i].name : k == 1 ? HEADS[i].name : ALIGN[i].name, i + 1);
            cBox[k].onChange = [this, k] { const int i = cBox[k].getSelectedItemIndex(); if (i >= 0 && i != (int) get (ck[k])) { set (ck[k], i); sink.hover (slot, hlAnalog, cBox[k].getText() + J (" — ") + J (compDesc ((int) k, i))); } };
            cBox[k].addMouseListener (this, false);
            addChildComponent (cBox[k]);
        }
        struct KD { const char* key; const char* label; const char* desc; };
        static const KD kd[] = { { "headroom", "Headroom", "HEADROOM：録音・再生アンプの電源電圧。下げるとテープより先にアンプが歪む" }, { "age", "Age", "AGE：テープとメカの経年。高域が下がり、飽和が早く、ヒスが増える" } };
        for (auto& d : kd)
        {
            auto k = std::make_unique<VKnob> (d.label, slot, hlAnalog, sink);
            const auto* sp = specOf (d.key);
            k->setLin (sp->lo, sp->hi, sp->step);
            const char* kc = d.key; const juce::String key (d.key); const char* desc = d.desc;
            k->get = [this, kc] { return get (kc); };
            k->set = [this, kc] (double v) { set (kc, v); };
            k->def = sp->def;
            k->text = [this, kc, key] { const double v = get (kc); return key == "headroom" ? juce::String (v, 1) + " V" : (v > 0 ? pct (v) : juce::String ("NEW")); };
            k->describe = [desc, t = k->text] { return J (desc) + J ("（現在 ") + t() + J ("）"); };
            addChildComponent (*k);
            cKnobs.push_back (std::move (k));
        }
        addChildComponent (harm);
        addChildComponent (circuitReset);
        circuitReset.onClick = [this]
        {
            for (auto* k : { "c1", "c2", "c3", "age" }) set (k, 0);
            set ("headroom", 24);
            sink.hover (slot, hlAnalog, J ("マシンを標準に戻しました"));
        };
    }
    build();
}

void ModulePanel::clearControls() { groups.clear(); knobs.clear(); segs.clear(); togs.clear(); syncers.clear(); owned.clear(); }
juce::String ModulePanel::structureKey() const
{
    const auto p = P();
    juce::String k;
    if (type == kTape) k << (ival (p.variant) == 2 ? "c" : "r");
    if (type == kDelay) k << ival (p.variant) << "/" << ival (p.stereo) << "/" << (ival (p.sync) > 0 ? 1 : 0) << "/" << (int) p.reverse;
    if (type == kReverb) k << ival (p.source) << "/" << (int) p.reverse << "/" << (ival (p.preSync) > 0 ? 1 : 0) << (ival (p.decaySync) > 0 ? 1 : 0) << (ival (p.revSync) > 0 ? 1 : 0);
    return k;
}
void ModulePanel::group (const juce::String& name, int hl) { groups.push_back ({ name, hl, {}, {}, {} }); }
void ModulePanel::addItem (juce::Component* c, int width) { addAndMakeVisible (c); groups.back().items.push_back (c); groups.back().widths.push_back (width); }
VKnob* ModulePanel::knob (const juce::String& label, int hl)
{
    auto* k = new VKnob (label, slot, hl, sink); owned.add (k); knobs.push_back (k); addItem (k, 70); return k;
}
VKnob* ModulePanel::lin (const char* key, const juce::String& label, double lo, double hi, double step, std::function<juce::String (double)> fmt, int hl, std::function<juce::String()> desc)
{
    auto* k = knob (label, hl); k->setLin (lo, hi, step);
    k->get = [this, key] { return get (key); }; k->set = [this, key] (double v) { set (key, v); };
    k->text = [this, key, fmt] { return fmt (get (key)); }; k->describe = desc;
    k->def = defaultsFor (type).*(specOf (key)->field);
    k->plainGet = [this, key] { return get (key); }; k->plainSet = [this, key] (double v) { set (key, v); }; k->plainLo = lo; k->plainHi = hi;
    return k;
}
VKnob* ModulePanel::logKnob (const char* key, const juce::String& label, double lo, double hi, std::function<juce::String (double)> fmt, int hl, std::function<juce::String()> desc)
{
    auto* k = knob (label, hl); k->setLog (lo, hi);
    k->get = [this, key, lo, hi] { return std::log (juce::jlimit (lo, hi, get (key))); };
    k->set = [this, key] (double v) { set (key, std::exp (v)); };
    k->text = [this, key, fmt] { return fmt (get (key)); }; k->describe = desc;
    k->def = std::log (juce::jlimit (lo, hi, defaultsFor (type).*(specOf (key)->field)));
    k->plainGet = [this, key] { return get (key); }; k->plainSet = [this, key] (double v) { set (key, v); }; k->plainLo = lo; k->plainHi = hi;
    const juce::String ks (key); k->msUnit = ks == "time" || ks == "predelay" || ks == "revLen";
    return k;
}
VKnob* ModulePanel::syncKnob (const char* key, const juce::String& label, int hl, const juce::String& title, const juce::String& what)
{
    juce::StringArray names; for (int i = 0; i < numNotes(); ++i) names.add (noteName (i));
    auto* k = listKnob (label, hl, names, [this, key] { return (int) get (key); }, [this, key] (int i) { set (key, i); }, 0,
        [this, key, what]
        {
            const int i = (int) get (key);
            return i > 0 ? what + J ("：") + noteName (i) + " = " + fmtMs (noteMs (i, proc.hostBpm.load())) + J ("（") + num (proc.hostBpm.load(), 2) + J (" BPM）。値をクリックすると 79 種の一覧から選べます")
                         : what + J ("：FREE（ミリ秒で設定）。値をクリックすると 79 種の音符の一覧から選べます（3 連符・付点・n/64 など）");
        });
    k->text = [this, key] { const int i = (int) get (key); return i > 0 ? noteName (i).upToFirstOccurrenceOf (" (", false, false) : juce::String ("FREE"); };
    k->onValueClick = [this, key, title, hl]
    {
        editor.openPicker (title, [this, key] { return (int) get (key); }, [this, key, title, hl] (int i)
        {
            set (key, i);
            sink.hover (slot, hl, title + J ("：") + (i > 0 ? noteName (i) + " = " + fmtMs (noteMs (i, proc.hostBpm.load())) : J ("None（FREE）")));
        });
    };
    return k;
}
VKnob* ModulePanel::listKnob (const juce::String& label, int hl, const juce::StringArray& names, std::function<int()> getI, std::function<void (int)> setI, int def, std::function<juce::String()> desc)
{
    auto* k = knob (label, hl); k->setList (names.size());
    k->get = [getI] { return (double) getI(); };
    k->set = [setI] (double v) { setI ((int) std::lround (v)); };
    k->text = [getI, names] { return names[juce::jlimit (0, names.size() - 1, getI())]; };
    k->describe = desc; k->def = def;
    return k;
}
Seg* ModulePanel::seg (const juce::String& caption, const juce::StringArray& names, int hl, std::function<int()> g, std::function<void (int)> s, std::function<juce::String (int)> desc)
{
    auto* sg = new Seg (caption, names, slot, hl, sink); owned.add (sg); segs.push_back (sg);
    sg->get = g; sg->set = s; sg->describe = desc; sg->sync();
    addItem (sg, sg->preferredWidth());
    return sg;
}
juce::TextButton* ModulePanel::tog (const juce::String& label, int hl, std::function<bool()> g, std::function<void (bool)> s, std::function<juce::String()> desc)
{
    auto* b = new juce::TextButton (label); owned.add (b);
    b->onClick = [this, b, g, s, hl, desc] { s (! g()); b->setToggleState (g(), juce::dontSendNotification); if (desc) sink.hover (slot, hl, desc()); };
    if (desc) b->onStateChange = [this, b, hl, desc] { if (b->isOver()) sink.hover (slot, hl, desc()); };
    b->setToggleState (g(), juce::dontSendNotification);
    togs.push_back ({ b, g });
    addItem (b, 78);
    return b;
}
void ModulePanel::mixOut (const juce::String& what)
{
    group ("OUT", hlMix);
    lin ("mix", "Mix", 0, 100, 1, pct, hlMix, [this, what] { return J ("MIX：原音と") + what + J ("のバランス。現在 ") + pct (get ("mix")) + (type == kTape ? juce::String() : J ("（50 % で原音・") + what + J ("とも 100 %）")); });
    lin ("output", "Output", -24, 24, 0.5, sdb, hlMix, [this] { return J ("OUTPUT：最終出力レベル ") + sdb (get ("output")); });
}
void ModulePanel::wetEq (int hl, const juce::String& where)
{
    group ("WET EQ", hl);
    lin ("eqLo", "Low", -12, 12, 0.5, sdb, hl, [this, where] { return J ("WET EQ LOW：") + where + J ("の 180 Hz 以下をシェルビングで ") + sdb (get ("eqLo")); });
    lin ("eqMid", "Mid", -12, 12, 0.5, sdb, hl, [this, where] { return J ("WET EQ MID：") + where + J ("の ") + hz (get ("eqMidF")) + J (" 付近をピーキングで ") + sdb (get ("eqMid")); });
    logKnob ("eqMidF", "Mid Freq", 200, 8000, hz, hl, [this] { return J ("WET EQ MID FREQ：") + hz (get ("eqMidF")); });
    lin ("eqHi", "High", -12, 12, 0.5, sdb, hl, [this, where] { return J ("WET EQ HIGH：") + where + J ("の 6 kHz 以上をシェルビングで ") + sdb (get ("eqHi")); });
}
void ModulePanel::chooseIR()
{
    chooser = std::make_unique<juce::FileChooser> (J ("IR ファイルを選択"), juce::File(), "*.wav;*.aif;*.aiff;*.flac");
    juce::Component::SafePointer<ModulePanel> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        auto* self = safe.getComponent(); if (self == nullptr) return;
        const auto f = fc.getResult(); if (! f.existsAsFile()) return;
        const auto err = self->proc.loadUserIR (self->slot, f);
        self->sink.hover (self->slot, hlRev, err.isNotEmpty() ? err : J ("IR「") + f.getFileName() + J ("」を読み込みました（サンプルレートは自動で変換）"));
    });
}

void ModulePanel::build()
{
    clearControls();
    key = structureKey();
    const auto p = P();
    auto onOff = [this] (const char* k) { return [this, k] { return get (k) > 0.5; }; };
    auto setB = [this] (const char* k) { return [this, k] (bool v) { set (k, v ? 1 : 0); }; };

    if (type == kTape)
    {
        const bool cas = ival (p.variant) == 2;
        group ("RECORD", hlTape);
        lin ("drive", "Input", -12, 18, 0.5, sdb, hlTape, [this] { return J ("INPUT：テープに送るレベル ") + sdb (get ("drive")) + J ("。上げるほど飽和し、高域から先に詰まる"); });
        seg ("Formula", { "MODERN", "CLASSIC", "VINTAGE" }, hlTape, [this] { return (int) get ("formula"); }, [this] (int i) { set ("formula", i); },
             [] (int i) { static const char* d[] = { "MODERN：高出力テープ（MOL +12 dB）。余裕が大きくクリーン", "CLASSIC：標準的なテープ（+9 dB）。適度に飽和", "VINTAGE：古いテープ（+6 dB）。早くつぶれて倍音が多く、ヒスも多い" }; return J (d[clampv (i, 0, 2)]); });
        lin ("bias", "Bias", -100, 100, 1, [] (double v) { return v == 0 ? juce::String ("CAL") : (v > 0 ? "OVER +" : "UNDER ") + juce::String ((int) std::abs (v)); }, hlTape,
             [this] { const double b = get ("bias"); return J ("BIAS：") + (b == 0 ? J ("適正（CAL）") : b > 0 ? J ("オーバーバイアス：高域が下がり、歪みが少なくなめらか") : J ("アンダーバイアス：高域が明るく、歪みが増えてざらつく")); });
        group ("TRANSPORT", hlTape);
        if (cas) seg ("Speed (ips)", { "1-7/8" }, hlTape, [] { return 0; }, [] (int) {}, [] (int) { return J ("カセットは 1-7/8 ips・70 µs 固定"); });
        else
        {
            const std::vector<double> sp (TAPE_SPEEDS, TAPE_SPEEDS + 4);
            seg ("Speed (ips)", { "3.75", "7.5", "15", "30" }, hlTape,
                 [this, sp] { int bi = 0; for (int i = 1; i < 4; ++i) if (std::abs (sp[(size_t) i] - get ("speed")) < std::abs (sp[(size_t) bi] - get ("speed"))) bi = i; return bi; },
                 [this, sp] (int i) { set ("speed", sp[(size_t) i]); },
                 [sp] (int i) { static const char* d[] = { "ローファイ", "ヴィンテージの太さ", "定番。低域が太く高域なめらか", "マスター用。最もワイドでクリーン" };
                                return "SPEED " + num (sp[(size_t) i], 2) + J (" ips：ヘッドバンプ（低域のふくらみ）が ") + juce::String ((int) std::lround (3.5 * sp[(size_t) i])) + J (" Hz 付近。") + J (d[i]); });
            seg ("EQ", { "NAB", "CCIR" }, hlTape, [this] { return (int) get ("eqStd"); }, [this] (int i) { set ("eqStd", i); },
                 [] (int i) { return i ? J ("CCIR（IEC）：ヨーロッパ式の録再 EQ。15 ips で 35 µs、高域がややなめらか") : J ("NAB：アメリカ式の録再 EQ。15 ips で 50 µs、ヘッドバンプがはっきり"); });
        }
        group ("MOTION", hlMotion);
        lin ("wow", "Wow", 0, 100, 1, offOr, hlMotion, [this] { return J ("WOW：ゆっくりした回転ムラ（0.55 Hz＋ランダム）") + (get ("analog") > 0.5 ? juce::String() : J ("（ANALOG オフでは無効）")); });
        lin ("flutter", "Flutter", 0, 100, 1, offOr, hlMotion, [this] { return J ("FLUTTER：速い回転ムラ（7〜12 Hz）。音がわずかにざらつく") + (get ("analog") > 0.5 ? juce::String() : J ("（ANALOG オフでは無効）")); });
        lin ("hiss", "Hiss", 0, 100, 1, offOr, hlMotion, [this] { return J ("HISS：テープヒス。50 で実機相当（テープの種類・速度で変わる）") + (get ("analog") > 0.5 ? juce::String() : J ("（ANALOG オフでは無効）")); });
        mixOut (J ("テープ音"));
    }
    else if (type == kDelay)
    {
        const int mode = clampv (ival (p.variant), 0, 3);
        const bool synced = ival (p.sync) > 0;
        const double mx = p.reverse != 0 ? std::min (DELAY_MAX[mode], REV_MAX) : DELAY_MAX[mode];
        group ("TIME", hlTime);
        auto* kt = logKnob ("time", "Time", 1, mx, fmtP, hlTime, [this, mx]
        {
            const auto q = P(); const double ms = delayMs (q, proc.hostBpm.load());
            return ival (q.sync) > 0 ? J ("TIME：テンポ同期中（") + noteName (ival (q.sync)) + " = " + fmtMs (ms) + " @ " + num (proc.hostBpm.load(), 2) + " BPM）" + (noteMs (ival (q.sync), proc.hostBpm.load()) > mx ? J ("。最大 ") + fmtT (mx) + J (" で頭打ち") : juce::String())
                                     : J ("TIME：") + fmtMs (q.time) + J ("（最大 ") + fmtT (mx) + J ("）。値をクリックすると 0.001 ms 単位で数値入力できます");
        });
        kt->text = [this] { return fmtP (delayMs (P(), proc.hostBpm.load())); };
        kt->setEnabled (! synced);
        syncKnob ("sync", "Sync", hlTime, "DELAY TIME SYNC", "SYNC");
        if (ival (p.stereo) == 3) lin ("ratioLR", "R / L", 10, 200, 1, pct, hlTime, [this] { return J ("R / L：右チャンネルの時間を左の ") + pct (get ("ratioLR")) + J (" に"); });
        if (mode == 1) seg ("Heads", { "1", "2", "3", "1+2", "2+3", "1+3", "1+2+3" }, hlTime, [this] { return (int) get ("heads"); }, [this] (int i) { set ("heads", i); },
                            [] (int) { return J ("PLAYBACK HEADS：再生ヘッドの組み合わせ（ヘッド 2・3 はヘッド 1 の 1.9 倍・2.8 倍の距離）。複数ヘッドでリズミカルな反復"); });
        group ("REPEATS", hlDelay);
        lin ("feedback", "Feedback", 0, 110, 1, pct, hlDelay, [this, mode] { return J ("FEEDBACK：繰り返しの量 ") + pct (get ("feedback")) + J ("。100 % を超えると発振（") + (mode == 0 || mode == 3 ? J ("リミッター") : J ("テープ／素子の飽和")) + J ("で頭打ち）"); });
        logKnob ("lowcut", "Low Cut", 19.9, 1000, cutLo, hlDelay, [this] { return J ("LOW CUT：繰り返すたびに低域を削る（") + cutLo (get ("lowcut")) + J ("）。左端で OFF"); });
        logKnob ("highcut", "High Cut", 1000, 20000, cutHi, hlDelay, [this] { return J ("HIGH CUT：繰り返すたびに高域を削る（") + cutHi (get ("highcut")) + J ("）。右端で OFF"); });
        lin ("drive", "Drive", -12, 12, 0.5, sdb, hlDelay, [this, mode] { return J ("DRIVE：ループに入れるレベル ") + sdb (get ("drive")) + J ("。") + (mode == 3 ? J ("12 bit 変換で上げすぎるとクリップ") : mode == 0 ? J ("デジタル HQ では透明（安全用のソフトリミッターのみ）") : J ("テープ／BBD の飽和が増える")); });
        group ("STEREO", hlTime);
        seg ("Mode", { "MONO", "STEREO", "PING-PONG", "DUAL" }, hlTime, [this] { return (int) get ("stereo"); }, [this] (int i) { set ("stereo", i); },
             [] (int i) { static const char* d[] = { "MONO：L+R を同じ時間で反復", "STEREO：左右それぞれを独立に反復", "PING-PONG：L → R → L … と交互に飛ぶ（入力は L+R を左から）", "DUAL：右の時間を R / L で左と変える（ワイド）" }; return J (d[clampv (i, 0, 3)]); });
        lin ("width", "Width", 0, 200, 1, pct, hlTime, [this] { return J ("WIDTH：エコーのステレオ幅 ") + pct (get ("width")) + J ("（0 % でモノ、200 % で広げる）"); });
        tog ("REVERSE", hlTime, onOff ("reverse"), setB ("reverse"), [this] { return get ("reverse") > 0.5 ? J ("REVERSE オン：TIME の長さごとに区切った音を逆再生して返す。逆回転ギター風。最大 2 秒") : J ("REVERSE：オンで逆再生ディレイ"); });
        group ("MOD", hlDelay);
        logKnob ("modRate", "Rate", 0.05, 10, [] (double v) { return juce::String (v, 2) + " Hz"; }, hlDelay, [this] { return J ("MOD RATE：揺れの速さ ") + juce::String (get ("modRate"), 2) + " Hz"; });
        lin ("modDepth", "Depth", 0, 100, 1, [] (double v) { return v > 0 ? juce::String (v * 0.03, 2) + " ms" : juce::String ("OFF"); }, hlDelay, [] { return J ("MOD DEPTH：反復のピッチの揺れ（コーラス的）。最大 3 ms"); });
        lin ("duck", "Duck", 0, 100, 1, [] (double v) { return v > 0 ? pct (v) : juce::String ("OFF"); }, hlDelay, [this] { return J ("DUCK：入力が鳴っている間はエコーを下げ、音が切れると戻す（ボーカル向け）。") + pct (get ("duck")); });
        if (mode == 1 || mode == 2) lin ("hiss", "Noise", 0, 100, 1, offOr, hlDelay, [mode] { return mode == 1 ? J ("NOISE：テープヒス（ANALOG オン時）") : J ("NOISE：BBD のノイズ（ANALOG オン時）"); });
        wetEq (hlEq, J ("エコー（ウェット音）"));
        mixOut (J ("エコー"));
    }
    else
    {
        const int src = clampv (ival (p.source), 0, 2);
        const bool algo = src == 0;
        group ("SOURCE", hlRev);
        seg ("Source", { "ALGO", "IR", "USER IR" }, hlRev, [this] { return (int) get ("source"); },
             [this] (int i) { set ("source", i); if (i == 2 && proc.userIRName (slot).isEmpty()) chooseIR(); },
             [] (int i) { static const char* d[] = { "ALGO：アルゴリズム・リバーブ（16 ラインのフィードバック・ディレイ・ネットワーク）。サイズ・残響時間・帯域ごとの減衰を自由に調整", "IR：内蔵のインパルスレスポンスで畳み込み（遅延ゼロ・倍精度）", "USER IR：手持ちの IR ファイル（WAV / AIFF など）を読み込んで畳み込み" }; return J (d[clampv (i, 0, 2)]); });
        auto* ib = new IRBox (src == 1 ? "Impulse" : src == 2 ? "User IR" : "Algorithm"); owned.add (ib);
        ib->addAndMakeVisible (ib->box); ib->addAndMakeVisible (ib->button);
        if (src == 1)
        {
            for (int i = 0; i < 7; ++i) ib->box.addItem (IR_NAMES[i], i + 1);
            ib->box.onChange = [this, ib] { const int i = ib->box.getSelectedItemIndex(); if (i >= 0 && i != (int) get ("irIndex")) { set ("irIndex", i); sink.hover (slot, hlRev, J ("IR：") + J (irDesc (i))); } };
            ib->button.setVisible (false);
            syncers.push_back ([this, ib] { const int i = (int) get ("irIndex"); if (ib->box.getSelectedItemIndex() != i) ib->box.setSelectedItemIndex (i, juce::dontSendNotification); });
        }
        else if (src == 2)
        {
            ib->box.setVisible (false);
            ib->button.setButtonText (J ("IR を読み込む…"));
            ib->button.onClick = [this] { chooseIR(); };
            syncers.push_back ([this, ib] { const auto n = proc.userIRName (slot); const auto t = n.isEmpty() ? J ("（未読み込み）") : n; if (ib->text != t) { ib->text = t; ib->repaint(); } });
        }
        else
        {
            ib->box.setVisible (false); ib->button.setVisible (false);
            syncers.push_back ([this, ib] { const auto t = variantNames (kReverb)[clampv ((int) get ("variant"), 0, 4)] + J (" · 16-line FDN"); if (ib->text != t) { ib->text = t; ib->repaint(); } });
        }
        addItem (ib, 170);
        if (! algo) lin ("irLen", "Length", 5, 100, 1, pct, hlRev, [this] { return J ("IR LENGTH：IR の長さを ") + pct (get ("irLen")) + J (" に切り詰める（末尾はフェード）"); });
        group ("SPACE", hlRev);
        auto RT = [this] { return resolveTimes (P(), proc.hostBpm.load()); };
        auto* kp = logKnob ("predelay", "Pre-delay", 0.1, 1000, fmtP, hlRev, [this, RT]
            { return ival (P().preSync) > 0 ? J ("PRE-DELAY：テンポ同期中（") + noteName (ival (P().preSync)) + " = " + fmtMs (RT().predelay) + J ("）") : J ("PRE-DELAY：直接音から残響が始まるまで ") + fmtMs (get ("predelay")) + J ("。値をクリックで数値入力"); });
        kp->text = [RT] { const double v = RT().predelay; return v < 0.15 ? juce::String ("0 ms") : fmtP (v); };
        kp->plainLo = 0;
        kp->setEnabled (ival (p.preSync) == 0);
        syncKnob ("preSync", "Sync", hlRev, "PRE-DELAY SYNC", "PRE-DELAY SYNC");
        if (algo)
        {
            lin ("size", "Size", 20, 200, 1, pct, hlRev, [this] { return J ("SIZE：空間の大きさ ") + pct (get ("size")) + J ("（ディレイラインと初期反射の長さ）"); });
            auto* kd = logKnob ("decay", "Decay", 0.1, 30, [] (double v) { return num (v, 3) + " s"; }, hlRev, [this, RT]
                { return ival (P().decaySync) > 0 ? J ("DECAY：テンポ同期中（RT60 = ") + noteName (ival (P().decaySync)) + " = " + num (RT().decay, 3) + J (" 秒）") : J ("DECAY：中域の残響時間（RT60）") + num (get ("decay"), 3) + J (" 秒。値をクリックで数値入力"); });
            kd->text = [RT] { return num (RT().decay, 3) + " s"; };
            kd->setEnabled (ival (p.decaySync) == 0);
            syncKnob ("decaySync", "Sync", hlRev, "DECAY SYNC (RT60)", "DECAY SYNC");
            logKnob ("loMult", "Low x", 0.25, 4, [] (double v) { return "x" + juce::String (v, 2); }, hlRev, [this] { return J ("LOW ×：250 Hz 以下の残響時間を中域の ") + juce::String (get ("loMult"), 2) + J (" 倍に"); });
            logKnob ("hiMult", "High x", 0.05, 2, [] (double v) { return "x" + juce::String (v, 2); }, hlRev, [this] { return J ("HIGH ×：3.5 kHz 以上の残響時間を中域の ") + juce::String (get ("hiMult"), 2) + J (" 倍に（小さいほど暗い）"); });
            group ("CHARACTER", hlRev);
            lin ("diffusion", "Diffuse", 0, 100, 1, pct, hlRev, [this] { return J ("DIFFUSION：入力の拡散 ") + pct (get ("diffusion")) + J ("。下げると粒立ちが出る"); });
            lin ("er", "ER", 0, 100, 1, pct, hlRev, [this] { return J ("EARLY REFLECTIONS：初期反射の量 ") + pct (get ("er")) + J ("（Plate はなし）"); });
            lin ("modulation", "Mod", 0, 100, 1, pct, hlRev, [this] { return J ("MODULATION：ディレイラインの揺れ ") + pct (get ("modulation")) + J ("。金属的な響きを消してなめらかに"); });
        }
        group ("REVERSE", hlRev);
        tog ("REVERSE", hlRev, onOff ("reverse"), setB ("reverse"), [this, algo] { return get ("reverse") > 0.5 ? J ("REVERSE オン：残響を時間反転した“吸い込む”リバーブ。音の後ろで膨らみ、ぷつっと切れる") + (algo ? J ("（ALGO はアルゴリズムの応答を反転して畳み込み）") : juce::String()) : J ("REVERSE：オンでリバース・リバーブ"); });
        if (p.reverse != 0)
        {
            auto* kr = logKnob ("revLen", "Length", 100, 6000, fmtP, hlRev, [this, RT]
                { return ival (P().revSync) > 0 ? J ("REVERSE LENGTH：テンポ同期中（") + noteName (ival (P().revSync)) + " = " + fmtMs (RT().revLen) + J ("）") : J ("REVERSE LENGTH：吸い込みの長さ ") + fmtMs (get ("revLen")) + J ("。値をクリックで数値入力（1 拍 = ") + fmtMs (60000 / proc.hostBpm.load()) + J ("）"); });
            kr->text = [RT] { return fmtP (RT().revLen); };
            kr->setEnabled (ival (p.revSync) == 0);
            syncKnob ("revSync", "Sync", hlRev, "REVERSE LENGTH SYNC", "REVERSE SYNC");
        }
        group ("WET CUT", hlEq);
        logKnob ("lowcut", "Low Cut", 19.9, 1000, cutLo, hlEq, [this] { return J ("LOW CUT：残響の低域を削る（") + cutLo (get ("lowcut")) + J ("）。左端で OFF"); });
        logKnob ("highcut", "High Cut", 1000, 20000, cutHi, hlEq, [this] { return J ("HIGH CUT：残響の高域を削る（") + cutHi (get ("highcut")) + J ("）。右端で OFF"); });
        wetEq (hlEq, J ("残響（ウェット音）"));
        group ("OUT", hlMix);
        lin ("width", "Width", 0, 200, 1, pct, hlMix, [this] { return J ("WIDTH：残響のステレオ幅 ") + pct (get ("width")); });
        lin ("mix", "Mix", 0, 100, 1, pct, hlMix, [this] { return J ("MIX：原音と残響のバランス ") + pct (get ("mix")) + J ("（50 % で原音・残響とも 100 %）"); });
        lin ("output", "Output", -24, 24, 0.5, sdb, hlMix, [this] { return J ("OUTPUT：最終出力レベル ") + sdb (get ("output")); });
    }
    for (auto& s : syncers) s();
    resized();
}

static int flow (std::vector<std::tuple<int, int>>& place, const std::vector<int>& widths, int width)
{
    int x = 0, row = 0; place.clear();
    for (auto w : widths) { if (x > 0 && x + w > width) { x = 0; ++row; } place.push_back ({ x, row }); x += w + 6; }
    return row + 1;
}
int ModulePanel::heightFor (int width) const
{
    std::vector<int> ws; for (auto& g : groups) { int w = 12; for (auto iw : g.widths) w += iw + 2; ws.push_back (w); }
    std::vector<std::tuple<int, int>> place;
    const int rows = flow (place, ws, width - railW - 28);
    return headH + rows * rowH + dynH + 14 + (editor.isMachineOpen (slot) && type == kTape ? circuitH : 0);
}
void ModulePanel::resized()
{
    auto r = getLocalBounds();
    auto rail = r.removeFromLeft (railW);
    up.setBounds (rail.getCentreX() - 14, 62, 28, 22);
    down.setBounds (rail.getCentreX() - 14, 90, 28, 22);
    r.reduce (14, 6);
    auto head = r.removeFromTop (34);
    power.setBounds (head.removeFromLeft (30).withSizeKeepingCentre (30, 30));
    delBtn.setBounds (head.removeFromRight (52).withSizeKeepingCentre (52, 24)); head.removeFromRight (6);
    dupBtn.setBounds (head.removeFromRight (52).withSizeKeepingCentre (52, 24)); head.removeFromRight (10);
    if (type != kReverb) { analogBtn.setBounds (head.removeFromRight (78).withSizeKeepingCentre (78, 26)); head.removeFromRight (6); }
    if (type == kTape) { machineBtn.setBounds (head.removeFromRight (64).withSizeKeepingCentre (64, 26)); head.removeFromRight (10); }
    presetBox.setBounds (head.removeFromRight (230).withSizeKeepingCentre (230, 28)); head.removeFromRight (6);
    const bool showVar = ! (type == kReverb && ival (P().source) > 0);
    variant.setVisible (showVar);
    if (showVar) { variant.setBounds (head.removeFromRight (150).withSizeKeepingCentre (150, 28)); head.removeFromRight (6); }
    subtitleRight = head.getRight();
    std::vector<int> ws; for (auto& g : groups) { int w = 12; for (auto iw : g.widths) w += iw + 2; ws.push_back (w); }
    std::vector<std::tuple<int, int>> place;
    const int rows = flow (place, ws, r.getWidth());
    controlsArea = r.removeFromTop (rows * rowH);
    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        auto& g = groups[gi];
        const int gx = controlsArea.getX() + std::get<0> (place[gi]), gy = controlsArea.getY() + 8 + std::get<1> (place[gi]) * rowH;
        g.r = { gx, gy, ws[gi], rowH - 12 };
        int x = gx + 6;
        for (size_t k = 0; k < g.items.size(); ++k)
        {
            auto* c = g.items[k];
            if (dynamic_cast<juce::TextButton*> (c) != nullptr) c->setBounds (x, gy + 14, g.widths[k], 28);
            else if (dynamic_cast<Seg*> (c) != nullptr) c->setBounds (x, gy + 6, g.widths[k], 56);
            else c->setBounds (x, gy + 6, g.widths[k], 86);
            x += g.widths[k] + 2;
        }
    }
    r.removeFromTop (4);
    dynArea = r.removeFromTop (dynH);
    {
        auto d = dynArea;
        lvArea = d.removeFromRight (120);
        hist.setBounds (d.removeFromRight (300)); d.removeFromRight (8);
        if (type == kTape) { views[0]->setBounds (d.removeFromLeft (190)); d.removeFromLeft (8); views[1]->setBounds (d); }
        else if (type == kDelay) views[0]->setBounds (d);
        else { views[1]->setBounds (d.removeFromRight (220)); d.removeFromRight (8); views[0]->setBounds (d); }
    }
    r.removeFromTop (8);
    circuitArea = r;
    const bool open = type == kTape && editor.isMachineOpen (slot);
    if (type == kTape)
    {
        for (auto& b : cBox) b.setVisible (open);
        for (auto& k : cKnobs) k->setVisible (open);
        harm.setVisible (open); circuitReset.setVisible (open);
    }
    if (open)
    {
        auto a = r.reduced (0, 4);
        auto hd = a.removeFromTop (26);
        circuitReset.setBounds (hd.withTrimmedLeft (430).removeFromLeft (80).reduced (0, 1));
        a.removeFromTop (6);
        harm.setBounds (a.removeFromRight (300).withHeight (124));
        auto comps = a.removeFromLeft (360);
        for (size_t k = 0; k < 3; ++k) cBox[k].setBounds (comps.getX() + 120, comps.getY() + 4 + (int) k * 36, 235, 28);
        a.removeFromLeft (10);
        for (auto& k : cKnobs) k->setBounds (a.removeFromLeft (70).withHeight (86).translated (0, 8));
    }
}
void ModulePanel::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (col::panel); g.fillRoundedRectangle (b, 6.f);
    g.setColour (dragging ? col::accent : col::line); g.drawRoundedRectangle (b.reduced (0.5f), 6.f, dragging ? 2.f : 1.f);
    auto rail = b.removeFromLeft ((float) railW);
    g.setColour (col::panel2); g.fillRoundedRectangle (rail, 6.f); g.fillRect (rail.withTrimmedLeft (6));
    g.setColour (col::accent); g.setFont (font (20.f, true));
    g.drawText (juce::String (slot + 1), rail.removeFromTop (34).toNearestInt(), juce::Justification::centred);
    g.setColour (col::inkDim);
    for (int yy = 0; yy < 3; ++yy) for (int xx = 0; xx < 2; ++xx) g.fillEllipse ((float) railW * 0.5f - 5 + (float) xx * 7, 38.f + (float) yy * 6, 3.2f, 3.2f);
    const bool on = proc.reader.on (slot);
    g.setColour (col::ink.withAlpha (on ? 1.f : 0.45f)); g.setFont (font (19.f, true));
    g.drawText (typeTitle (type), railW + 54, 6, 260, 34, juce::Justification::left);
    g.setColour (col::inkDim); g.setFont (font (12.f));
    {
        int sx = railW + 54 + (int) textW (font (19.f, true), typeTitle (type)) + 12;
        // the first module of each type is the one the host's automation parameters drive
        const bool mapped = proc.reader.mappedSlot (type) == slot;
        const juce::String tag = mapped ? "AUTOMATION" : J ("オートメーション対象外");
        const int tw = (int) textW (font (10.5f, true), tag) + 12;
        const juce::Rectangle<float> tr ((float) sx, 14.f, (float) tw, 18.f);
        g.setColour (mapped ? col::accent.withAlpha (0.18f) : col::panel2); g.fillRoundedRectangle (tr, 3.f);
        g.setColour (mapped ? col::accent : col::inkDim); g.drawRoundedRectangle (tr, 3.f, 1.f);
        g.setFont (font (10.5f, true)); g.drawText (tag, tr.toNearestInt(), juce::Justification::centred);
        sx += tw + 10;
        g.setColour (col::inkDim); g.setFont (font (12.f));
        if (subtitleRight - sx > 60) g.drawFittedText (J (subtitleOf (type)), sx, 6, subtitleRight - sx - 8, 34, juce::Justification::centredLeft, 1, 1.0f);
    }
    const int hlS = editor.highlightSlot(), hlB = editor.highlightBand();
    for (auto& gr : groups)
    {
        auto gb = gr.r.toFloat();
        if (gr.hl != hlNone)
        {
            const auto c = hlColour (gr.hl); const bool lit = hlS == slot && hlB == gr.hl;
            g.setColour (c.withAlpha (lit ? 0.16f : 0.07f)); g.fillRoundedRectangle (gb, 6.f);
            g.setColour (c.withAlpha (lit ? 1.f : 0.35f)); g.drawRoundedRectangle (gb.reduced (0.5f), 6.f, 1.f);
        }
        if (gr.name.isNotEmpty())
        {
            const int w = (int) textW (font (11.f, true), gr.name) + 8;
            g.setFont (font (11.f, true));
            g.setColour (col::panel); g.fillRect (gr.r.getX() + 8, gr.r.getY() - 7, w, 13);
            g.setColour (gr.hl != hlNone ? hlColour (gr.hl) : col::inkDim);
            g.drawText (gr.name, gr.r.getX() + 8, gr.r.getY() - 7, w, 13, juce::Justification::centred);
        }
    }
    {
        auto a = lvArea.toFloat().reduced (6, 12);
        auto bar = [&] (const char* lab, float v, juce::Colour c)
        {
            g.setColour (col::inkDim); g.setFont (mono (10.5f)); g.drawText (lab, a.removeFromTop (14).toNearestInt(), juce::Justification::left);
            auto br = a.removeFromTop (8); a.removeFromTop (8);
            g.setColour (juce::Colour (0xff111317)); g.fillRoundedRectangle (br, 2);
            const float t = juce::jlimit (0.f, 1.f, (juce::Decibels::gainToDecibels (v, -60.f) + 48) / 48);
            g.setColour (c); g.fillRoundedRectangle (br.withWidth (br.getWidth() * t), 2);
        };
        bar ("IN", lvI, col::good); bar ("OUT", lvO, col::good);
        if (type != kTape) bar ("WET", lvW, col::rev);
    }
    if (type == kTape && editor.isMachineOpen (slot))
    {
        const float y = (float) circuitArea.getY();
        const float dash[] = { 4.f, 4.f };
        g.setColour (col::line); g.drawDashedLine ({ (float) circuitArea.getX(), y, (float) circuitArea.getRight(), y }, dash, 2, 1.f);
        g.setColour (col::analog); g.setFont (font (13.f, true));
        g.drawText ("MACHINE", circuitArea.getX(), circuitArea.getY() + 6, 80, 18, juce::Justification::left);
        g.setColour (col::inkDim); g.setFont (font (11.5f));
        g.drawText (P().analog > 0.5 ? J ("電子回路・ヘッド・録音レベルを替えると倍音と帯域が変わります") : J ("ANALOG オフのため飽和・ノイズは無効です"),
                    circuitArea.getX() + 80, circuitArea.getY() + 6, 340, 18, juce::Justification::left);
        g.setFont (font (12.f));
        const char* labels[3] = { "Electronics", "Heads", "Alignment" };
        for (size_t k = 0; k < 3; ++k) if (cBox[k].isVisible()) g.drawText (labels[k], circuitArea.getX(), cBox[k].getY(), 116, cBox[k].getHeight(), juce::Justification::centredLeft);
    }
}
void ModulePanel::tick()
{
    if (structureKey() != key)
    {
        bool busy = false; for (auto* k : knobs) busy |= k->slider.isMouseButtonDown();
        if (! busy) { build(); editor.layoutRack(); }
    }
    const auto p = P();
    power.setToggleState (proc.reader.on (slot), juce::dontSendNotification);
    analogBtn.setToggleState (p.analog > 0.5, juce::dontSendNotification);
    machineBtn.setToggleState (editor.isMachineOpen (slot), juce::dontSendNotification);
    if (variant.getSelectedItemIndex() != ival (p.variant)) variant.setSelectedItemIndex (ival (p.variant), juce::dontSendNotification);
    for (auto* k : knobs) k->sync();
    for (auto* s : segs) s->sync();
    for (auto& t : togs) t.first->setToggleState (t.second(), juce::dontSendNotification);
    for (auto& s : syncers) s();
    const float i = proc.lvIn[(size_t) slot].exchange (0.f), o = proc.lvOut[(size_t) slot].exchange (0.f), w = proc.lvWet[(size_t) slot].exchange (0.f);
    const bool live = proc.reader.on (slot);
    lvI = live ? std::max (i, lvI * 0.8f) : 0; lvO = live ? std::max (o, lvO * 0.8f) : 0; lvW = live ? std::max (w, lvW * 0.8f) : 0;
    hist.push (live ? i : 0, live ? o : 0, live ? w : 0);
    const int hs = editor.highlightSlot(), hb = editor.highlightBand();
    for (auto& v : views)
    {
        bool lit = hs == slot;
        if (type == kTape) lit = lit && (hb == hlTape || hb == hlMotion || hb == hlAnalog);
        else if (type == kDelay) lit = lit && (hb == hlTime || hb == hlDelay || hb == hlEq);
        else lit = lit && (hb == hlRev || hb == hlEq);
        v->lit = lit;
        v->refresh (live ? i : 0);
    }
    if (type == kTape && editor.isMachineOpen (slot))
    {
        static const char* ck[3] = { "c1", "c2", "c3" };
        for (size_t k = 0; k < 3; ++k) { const int idx = (int) get (ck[k]); if (cBox[k].getSelectedItemIndex() != idx) cBox[k].setSelectedItemIndex (idx, juce::dontSendNotification); cBox[k].setAlpha (p.analog > 0.5 ? 1.f : 0.4f); }
        for (auto& k : cKnobs) { k->sync(); k->setAlpha (p.analog > 0.5 ? 1.f : 0.4f); }
        harm.refresh();
    }
    repaint (lvArea);
}
void ModulePanel::mouseEnter (const juce::MouseEvent& e)
{
    for (size_t k = 0; k < 3; ++k)
        if (e.eventComponent == &cBox[k]) sink.hover (slot, hlAnalog, cBox[k].getText() + J (" — ") + J (compDesc ((int) k, juce::jmax (0, cBox[k].getSelectedItemIndex()))));
}
void ModulePanel::mouseDown (const juce::MouseEvent& e) { if (e.eventComponent != this) return; dragging = e.x < railW && e.y > 30 && e.y < 60; repaint(); }
void ModulePanel::mouseDrag (const juce::MouseEvent& e) { if (dragging) editor.dragPanel (*this, e.getEventRelativeTo (getParentComponent()).y, false); }
void ModulePanel::mouseUp (const juce::MouseEvent& e) { if (dragging) { dragging = false; editor.dragPanel (*this, e.getEventRelativeTo (getParentComponent()).y, true); repaint(); } }

// ============================================================================ FreqMap
FreqMap::FreqMap (TapeSpaceRackProcessor& p, HoverSink& s) : proc (p), sink (s)
{
    for (int i = 0; i < NF; ++i) freqs.push_back (20.0 * std::pow (1000.0, i / (double) (NF - 1)));
    slots.resize ((size_t) numSlots());
    spec.fill (-100.f);
}
juce::Rectangle<float> FreqMap::plot() const { return getLocalBounds().toFloat().withTrimmedLeft (46).withTrimmedRight (20).withTrimmedTop (34).withTrimmedBottom (22); }
float FreqMap::xOf (double f) const { auto r = plot(); return r.getX() + (float) (std::log (f / 20.0) / std::log (1000.0)) * r.getWidth(); }
float FreqMap::yOf (double db) const { auto r = plot(); const double R = range; return r.getY() + (float) (1 - (juce::jlimit (-R, R, db) + R) / (2 * R)) * r.getHeight(); }
void FreqMap::tick()
{
    const double bpm = proc.hostBpm.load();
    for (int s = 0; s < numSlots(); ++s)
    {
        auto& c = slots[(size_t) s];
        const int t = proc.reader.type (s);
        c.on = proc.reader.on (s);
        if (t < 0) { c.type = -1; continue; }
        const auto p = proc.reader.params (s);
        if (c.valid && c.type == t && p == c.p && (t != kDelay || c.bpm == bpm)) continue;
        c.type = t; c.p = p; c.valid = true; c.bpm = bpm;
        const auto v = responseTone (t, p, 48000, freqs, bpm);
        for (int i = 0; i < NF; ++i) c.tone[(size_t) i] = v[(size_t) i];
    }
    const int ready = proc.specFifo.getNumReady();
    if (ready > 0)
    {
        const auto scope = proc.specFifo.read (ready);
        auto take = [&] (int start, int size) { for (int i = 0; i < size; ++i) { ring[(size_t) ringPos] = proc.specBuffer[(size_t) (start + i)]; ringPos = (ringPos + 1) % TapeSpaceRackProcessor::fftSize; } };
        take (scope.startIndex1, scope.blockSize1); take (scope.startIndex2, scope.blockSize2);
        const int N = TapeSpaceRackProcessor::fftSize;
        for (int i = 0; i < N; ++i) fftData[(size_t) i] = ring[(size_t) ((ringPos + i) % N)];
        window.multiplyWithWindowingTable (fftData.data(), (size_t) N);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        const double sr = proc.getHostRate();
        for (int i = 0; i < NF; ++i)
        {
            const int bin = juce::jlimit (1, N / 2 - 1, (int) std::lround (freqs[(size_t) i] / sr * N));
            const float db = juce::Decibels::gainToDecibels (fftData[(size_t) bin] / (N * 0.25f), -120.f);
            spec[(size_t) i] = spec[(size_t) i] * 0.7f + db * 0.3f;
        }
        hasSpec = true;
    }
    repaint();
}
void FreqMap::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (col::panel); g.fillRoundedRectangle (bounds, 6.f);
    g.setColour (col::line); g.drawRoundedRectangle (bounds.reduced (0.5f), 6.f, 1.f);
    g.setColour (col::ink); g.setFont (font (15.f, true)); g.drawText ("FREQUENCY MAP", 12, 8, 160, 18, juce::Justification::left);
    struct Leg { juce::String t; juce::Colour c; int style; };
    const Leg legs[] = { { J ("テープの音色（全体を通る音）"), juce::Colours::white, 0 }, { J ("ディレイの反復音（ループ＋ウェット EQ）"), col::delay, 1 },
                         { J ("リバーブの残響（ウェット EQ）"), col::rev, 1 }, { J ("出力スペクトル"), juce::Colour (0xff5b6474), 0 } };
    float lx = 170; g.setFont (font (12.f));
    for (auto& l : legs)
    {
        g.setColour (l.c);
        if (l.style) { const float dl[] = { 4, 3 }; g.drawDashedLine ({ lx, 17.5f, lx + 12, 17.5f }, dl, 2, 2.f); }
        else g.fillRoundedRectangle (lx, 12.5f, 10, 10, 2);
        g.setColour (col::inkDim); const int w = (int) textW (font (12.f), l.t);
        g.drawText (l.t, (int) lx + 15, 9, w + 4, 16, juce::Justification::left); lx += (float) w + 30;
    }
    auto r = plot();
    if (hasSpec)
    {
        juce::Path sp; sp.startNewSubPath (r.getX(), r.getBottom());
        for (int i = 0; i < NF; ++i) { const float v = juce::jlimit (-100.f, -10.f, spec[(size_t) i]); sp.lineTo (xOf (freqs[(size_t) i]), r.getBottom() - (v + 100) / 90 * r.getHeight()); }
        sp.lineTo (r.getRight(), r.getBottom()); sp.closeSubPath();
        g.setColour (juce::Colour (0xff5b6474).withAlpha (0.22f)); g.fillPath (sp);
    }
    g.setFont (mono (10.5f));
    for (double f : { 20., 50., 100., 200., 500., 1000., 2000., 5000., 10000., 20000. })
    {
        g.setColour (juce::Colour (0xff2c313a)); g.drawVerticalLine ((int) xOf (f), r.getY(), r.getBottom());
        g.setColour (juce::Colour (0xff7d8492));
        g.drawText (f >= 1000 ? juce::String ((int) (f / 1000)) + "k" : juce::String ((int) f), (int) xOf (f) - 20, (int) r.getBottom() + 3, 40, 14, juce::Justification::centred);
    }
    const double step = range >= 24 ? 6 : range >= 12 ? 3 : 1;
    for (double d = std::ceil ((-range + 1e-6) / step) * step; d < range; d += step)
    {
        g.setColour (std::abs (d) < 1e-6 ? juce::Colour (0xff4a515e) : juce::Colour (0xff2c313a)); g.drawHorizontalLine ((int) yOf (d), r.getX(), r.getRight());
        g.setColour (juce::Colour (0xff7d8492)); g.drawText ((d > 0 ? "+" : "") + juce::String ((int) d), (int) r.getX() - 40, (int) yOf (d) - 7, 34, 14, juce::Justification::right);
    }
    auto pathOf = [&] (const Curve& c) { juce::Path p; for (int i = 0; i < NF; ++i) { const juce::Point<float> pt (xOf (freqs[(size_t) i]), yOf (c[(size_t) i])); i ? p.lineTo (pt) : p.startNewSubPath (pt); } return p; };
    auto dashed = [&] (const juce::Path& p, float w) { juce::Path d; const float pat[] = { 6, 4 }; juce::PathStrokeType (w).createDashedStroke (d, p, pat, 2); return d; };
    Curve total {};
    for (int s = 0; s < numSlots(); ++s)
    {
        auto& c = slots[(size_t) s];
        if (c.type < 0) continue;
        const bool lit = hlSlot == s;
        if (c.type == kTape) { if (c.on) for (int i = 0; i < NF; ++i) total[(size_t) i] += c.tone[(size_t) i] * c.p.mix / 100; if (! lit) continue; }
        const auto colr = c.type == kTape ? col::tape : c.type == kDelay ? col::delay : col::rev;
        g.setColour (colr.withAlpha (c.on ? (hlSlot >= 0 && ! lit ? 0.3f : 1.f) : 0.25f));
        if (c.type == kTape) g.strokePath (pathOf (c.tone), juce::PathStrokeType (3.f)); else g.fillPath (dashed (pathOf (c.tone), lit ? 3.f : 1.8f));
        if (hlSlot < 0 || lit)
        {
            const int li = c.type == kDelay ? 150 : c.type == kReverb ? 190 : 120;
            g.setFont (lit ? font (12.f, true) : mono (10.5f));
            g.drawText (juce::String (s + 1) + " " + juce::String (typeTitle (c.type)) + (c.type == kDelay ? J ("（反復 1 回目）") : c.type == kReverb ? J ("（残響）") : juce::String()),
                        (int) xOf (freqs[(size_t) li]) + 4, (int) yOf (c.tone[(size_t) li]) + 4, 260, 14, juce::Justification::left);
        }
    }
    g.setColour (juce::Colours::white); g.strokePath (pathOf (total), juce::PathStrokeType (2.5f));
    if (range >= 24)
    {
        g.setColour (col::inkDim); g.setFont (font (11.5f));
        g.drawText (J ("白はテープを通った音の周波数特性（ヘッドバンプ・高域）。点線はエコーと残響の音色。テープの細かな変化は ±4 dB で拡大できます"), (int) r.getX() + 8, (int) r.getY() + 3, 900, 16, juce::Justification::left);
    }
}
void FreqMap::mouseMove (const juce::MouseEvent& e)
{
    auto r = plot(); const double f = 20.0 * std::pow (1000.0, (e.position.x - r.getX()) / r.getWidth());
    if (f < 20 || f > 20000) return;
    const int i = juce::jlimit (0, NF - 1, (int) std::lround (std::log (f / 20) / std::log (1000.0) * (NF - 1)));
    juce::StringArray parts;
    for (int s = 0; s < numSlots(); ++s)
    {
        auto& c = slots[(size_t) s];
        if (c.type < 0 || ! c.on) continue;
        parts.add (juce::String (s + 1) + J (" 番 ") + typeTitle (c.type) + J ("：") + sdb (c.tone[(size_t) i]));
    }
    sink.hover (-1, hlNone, "<" + hz (f) + J ("> 付近 — ") + (parts.isEmpty() ? J ("モジュールなし") : parts.joinIntoString (J (" ／ "))));
}

// ============================================================================ Editor
TapeSpaceRackEditor::TapeSpaceRackEditor (TapeSpaceRackProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&look);
    map = std::make_unique<FreqMap> (p, *this); addAndMakeVisible (*map);
    for (auto* b : { &r24, &r12, &r4 }) addAndMakeVisible (*b);
    r24.onClick = [this] { map->range = 24; timerCallback(); };
    r12.onClick = [this] { map->range = 12; timerCallback(); };
    r4.onClick = [this] { map->range = 4; timerCallback(); };
    static const char* addNames[] = { "Tape Machine", "Echo / Delay", "Space Reverb" };
    for (int t = 0; t < kNumTypes; ++t)
    {
        auto* b = addBtns.add (new juce::TextButton (J ("＋ ") + addNames[t]));
        b->onClick = [this, t] { if (! proc.addModule (t)) hover (-1, hlNone, J ("スロットがいっぱいです（最大 8 台）")); };
        addAndMakeVisible (b);
    }
    headerPreset.setTextWhenNothingSelected (J ("ラックプリセット…"));
    for (int i = 0; i < proc.presets.numRack(); ++i) headerPreset.addItem (proc.presets.rackName (i), i + 1);
    const juce::Component::SafePointer<TapeSpaceRackEditor> safeThis (this);
    headerPreset.onChange = [this, safeThis]
    {
        const int idx = headerPreset.getSelectedItemIndex(); if (idx < 0) return;
        const auto name = headerPreset.getText();
        headerPreset.setSelectedId (0, juce::dontSendNotification);
        juce::MessageManager::callAsync ([safeThis, idx, name] { if (auto* self = safeThis.getComponent()) self->applyPreset (idx, name); });
    };
    addAndMakeVisible (headerPreset);
    bpm.setSliderStyle (juce::Slider::IncDecButtons);
    bpm.setRange (40, 240, 0.5);
    bpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 26);
    bpm.setColour (juce::Slider::textBoxBackgroundColourId, col::panel2);
    bpm.setColour (juce::Slider::textBoxOutlineColourId, col::line);
    bpm.setColour (juce::Slider::textBoxTextColourId, col::ink);
    bpm.setValue (proc.hostBpm.load(), juce::dontSendNotification);
    bpm.onValueChange = [this] { if (! proc.bpmFromHost.load()) proc.hostBpm.store (bpm.getValue()); };
    addAndMakeVisible (bpm);
    viewport.setViewedComponent (&rackContent, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    rebuildPanels();
    hover (-1, hlNone, {});
    addChildComponent (picker);
    setResizable (true, true); setResizeLimits (kW, 640, kW, 1800); setSize (kW, 960);
    startTimerHz (30);
}
TapeSpaceRackEditor::~TapeSpaceRackEditor() { setLookAndFeel (nullptr); }

void TapeSpaceRackEditor::applyPreset (int idx, const juce::String& name)
{
    machineOpen.fill (false);
    proc.applyRackPreset (idx);
    proc.currentProgram = idx;
    hover (-1, hlNone, J ("ラックプリセット「") + name + J ("」を読み込みました"));
    layoutRack();
}
void TapeSpaceRackEditor::rebuildPanels()
{
    signature.clear();
    for (int s = 0; s < numSlots(); ++s) signature.push_back (proc.reader.type (s));
    panels.clear();
    for (int s = 0; s < numSlots(); ++s) if (signature[(size_t) s] >= 0) panels.push_back (std::make_unique<ModulePanel> (*this, proc, *this, s, signature[(size_t) s]));
    for (auto& pn : panels) rackContent.addAndMakeVisible (*pn);
    layoutRack();
}
void TapeSpaceRackEditor::layoutRack()
{
    const int w = std::max (viewport.getWidth() - viewport.getScrollBarThickness() - 4, 400);
    int y = 0;
    for (auto& pn : panels)
    {
        pn->setBounds (0, y, w, pn->heightFor (w)); y += pn->getHeight() + kGap;
        pn->up.setEnabled (pn->slot > 0); pn->down.setEnabled (pn->slot < (int) panels.size() - 1);
    }
    rackContent.setSize (w, std::max (y - kGap, 1));
    for (auto* b : addBtns) b->setEnabled ((int) panels.size() < numSlots());
}
void TapeSpaceRackEditor::hover (int slot, int hl, const juce::String& text)
{
    hlSlot = slot; hlBand = hl;
    hint = text.isNotEmpty() ? text : J ("つまみにカーソルを合わせると、関係する表示（テープのカーブ・エコーの並び・残響の形・周波数マップの曲線）が同じ色で光ります。");
    if (map) { map->hlSlot = slot; map->hl = hl; map->repaint(); }
    for (auto& pn : panels) pn->repaint();
    repaint (hintArea);
}
void TapeSpaceRackEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto head = getLocalBounds().reduced (kMargin).removeFromTop (kHeader).toFloat();
    g.setColour (col::panel); g.fillRoundedRectangle (head, 6.f);
    g.setColour (col::line); g.drawRoundedRectangle (head.reduced (0.5f), 6.f, 1.f);
    g.setColour (col::ink); g.setFont (font (22.f, true));
    g.drawText ("TAPE SPACE RACK", (int) head.getX() + 14, (int) head.getY(), 300, (int) head.getHeight(), juce::Justification::left);
    g.setColour (col::inkDim); g.setFont (font (12.f));
    const int sx = (int) head.getX() + 14 + (int) textW (font (22.f, true), "TAPE SPACE RACK") + 14;
    g.drawFittedText (J ("テープ・ディレイ・リバーブ ラック ― 上から順に信号が流れます"), sx, (int) head.getY(), headerPreset.getX() - sx - 10, (int) head.getHeight(), juce::Justification::centredLeft, 1, 1.0f);
    g.setFont (mono (11.f));
    g.drawText (proc.bpmFromHost.load() ? "BPM (HOST) " + num (proc.hostBpm.load(), 1) : juce::String ("BPM"), bpmArea, juce::Justification::centredLeft);
    auto m = meterArea.toFloat();
    auto drawMeter = [&] (juce::Rectangle<float> r, const char* lab, float lvl)
    {
        g.setColour (col::inkDim); g.setFont (mono (11.f)); g.drawText (lab, r.removeFromLeft (30).toNearestInt(), juce::Justification::centredRight);
        r.removeFromLeft (6); r = r.withSizeKeepingCentre (r.getWidth(), 8);
        g.setColour (col::panel2); g.fillRoundedRectangle (r, 2);
        const float t = juce::jlimit (0.f, 1.f, (juce::Decibels::gainToDecibels (lvl, -60.f) + 48) / 48);
        juce::ColourGradient cg (col::good, r.getX(), 0, col::bad, r.getRight(), 0, false); cg.addColour (0.7, col::good); cg.addColour (0.8, col::warn);
        g.setGradientFill (cg); g.fillRoundedRectangle (r.withWidth (r.getWidth() * t), 2);
    };
    drawMeter (m.removeFromLeft (m.getWidth() / 2).reduced (4, 0), "IN", inLvl);
    drawMeter (m.reduced (4, 0), "OUT", outLvl);
    g.setFont (font (13.f));
    auto h = hintArea;
    if (hlBand != hlNone) { g.setColour (hlColour (hlBand)); g.fillEllipse ((float) h.getX() + 4, (float) h.getCentreY() - 5, 10, 10); }
    g.setColour (col::ink); g.drawFittedText (hint, h.withTrimmedLeft (22), juce::Justification::centredLeft, 1, 0.9f);
    g.setColour (col::inkDim); g.setFont (font (12.f));
    g.drawText (J ("追加:"), toolbarArea.getX(), toolbarArea.getY(), 40, toolbarArea.getHeight(), juce::Justification::centredLeft);
    g.drawText (juce::String ((int) panels.size()) + J (" 台／8"), toolbarArea.getRight() - 80, toolbarArea.getY(), 76, toolbarArea.getHeight(), juce::Justification::centredRight);
    if (panels.empty()) g.drawText (J ("モジュールがありません。上の「＋」ボタンで追加してください。"), viewport.getBounds(), juce::Justification::centred);
}
void TapeSpaceRackEditor::paintOverChildren (juce::Graphics& g)
{
    if (dropY >= 0) { g.setColour (col::accent); g.fillRect (viewport.getX(), dropY - 2, viewport.getWidth(), 3); }
}
void TapeSpaceRackEditor::resized()
{
    auto r = getLocalBounds().reduced (kMargin);
    auto head = r.removeFromTop (kHeader);
    meterArea = head.removeFromRight (260).reduced (14, 0);
    auto bp = head.removeFromRight (230).reduced (6, 0);
    bpmArea = bp.removeFromLeft (120);
    bpm.setBounds (bp.withSizeKeepingCentre (bp.getWidth(), 26));
    headerPreset.setBounds (head.removeFromRight (280).withSizeKeepingCentre (280, 30));
    r.removeFromTop (kGap);
    auto mapR = r.removeFromTop (kMap);
    map->setBounds (mapR);
    {
        auto rb = mapR.reduced (12, 6).removeFromTop (24);
        r4.setBounds (rb.removeFromRight (58)); rb.removeFromRight (4);
        r12.setBounds (rb.removeFromRight (64)); rb.removeFromRight (4);
        r24.setBounds (rb.removeFromRight (66));
        for (auto* b : { &r24, &r12, &r4 }) b->toFront (false);
    }
    r.removeFromTop (kGap);
    hintArea = r.removeFromTop (kHint);
    r.removeFromTop (kGap);
    toolbarArea = r.removeFromTop (kToolbar);
    {
        auto t = toolbarArea;
        t.removeFromLeft (40);
        for (auto* b : addBtns) { b->setBounds (t.removeFromLeft (150).reduced (0, 3)); t.removeFromLeft (6); }
    }
    r.removeFromTop (kGap);
    viewport.setBounds (r);
    picker.setBounds (getLocalBounds());
    layoutRack();
}
void TapeSpaceRackEditor::dragPanel (ts::ui::ModulePanel& pn, int yInRack, bool finished)
{
    const int yEd = yInRack + viewport.getY() - viewport.getViewPositionY();
    if (! finished) { dropY = juce::jlimit (viewport.getY(), viewport.getBottom(), yEd); repaint(); return; }
    dropY = -1;
    int target = (int) panels.size() - 1;
    for (auto& other : panels) if (yInRack < other->getBounds().getCentreY()) { target = other->slot - (other->slot > pn.slot ? 1 : 0); break; }
    proc.moveSlot (pn.slot, juce::jlimit (0, (int) panels.size() - 1, target));
    repaint();
}
void TapeSpaceRackEditor::timerCallback()
{
    inLvl = std::max (proc.inPeak.exchange (0.f), inLvl * 0.85f);
    outLvl = std::max (proc.outPeak.exchange (0.f), outLvl * 0.85f);
    repaint (meterArea);
    const bool host = proc.bpmFromHost.load();
    bpm.setEnabled (! host);
    if (host && std::abs (bpm.getValue() - proc.hostBpm.load()) > 0.01) bpm.setValue (proc.hostBpm.load(), juce::dontSendNotification);
    repaint (bpmArea);
    bool changed = false;
    for (int s = 0; s < numSlots(); ++s) if (proc.reader.type (s) != signature[(size_t) s]) changed = true;
    if (changed) { hover (-1, hlNone, {}); rebuildPanels(); repaint(); }
    for (size_t i = 0; i < panels.size(); ++i) panels[i]->tick();
    for (auto* b : { &r24, &r12, &r4 }) b->setToggleState (false, juce::dontSendNotification);
    (map->range >= 24 ? r24 : map->range >= 12 ? r12 : r4).setToggleState (true, juce::dontSendNotification);
    map->tick();
    if (dropY >= 0) repaint();
}
