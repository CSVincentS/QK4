# Patterns

Signal/slot patterns and feature addition guides for QK4.

## Signal/Slot Connection Patterns

### RadioState → MainWindow

```cpp
connect(m_radioState, &RadioState::frequencyChanged,
        this, &MainWindow::onFrequencyChanged);
connect(m_radioState, &RadioState::modeChanged,
        this, &MainWindow::onModeChanged);
```

### Widget → Lambda (inline handling)

```cpp
connect(m_spanUpBtn, &QPushButton::clicked, this, [this]() {
    int newSpan = m_panadapterA->span() + 1000;
    m_tcpClient->sendCAT(QString("SPAN%1;").arg(newSpan));
});
```

### Custom Widget Signals

```cpp
// In PanadapterWidget
signals:
    void frequencyClicked(qint64 frequency);
    void frequencyScrolled(int direction);

// Connection in MainWindow
connect(m_panadapterA, &PanadapterWidget::frequencyClicked,
        this, [this](qint64 freq) {
    m_tcpClient->sendCAT(QString("FA%1;").arg(freq, 11, 10, QChar('0')));
});
```

---

## Adding New Features

### Adding a New Widget

1. Create `src/<category>/<widgetname>.cpp/.h`
2. Subclass `QWidget`, implement `paintEvent()` if custom drawing
3. Add to `CMakeLists.txt` SOURCES and HEADERS
4. Include in `mainwindow.h`, create instance in `setupUi()`
5. Connect signals/slots in MainWindow constructor

**GPU-accelerated widgets:** For high-performance rendering (spectrum, waterfall), subclass `QRhiWidget` instead of `QWidget`. See `src/dsp/panadapter_rhi.cpp` for the pattern. Requires shader compilation via `qt6_add_shaders()` in CMakeLists.txt.

### Adding a New Menu Item

```cpp
// In MainWindow::setupMenuBar()
QMenu *myMenu = menuBar()->addMenu("&MyMenu");
QAction *myAction = myMenu->addAction("My Action");
connect(myAction, &QAction::triggered, this, &MainWindow::onMyAction);
```

### Adding RadioState Property / CAT Command

Post-Phase 1: state lives in **subsystem structs** under `src/models/radiostate/`, not as raw members on `RadioState`. The flow is:

1. **Pick the subsystem.** Field lives in whichever subsystem matches its topic — see the table in "Subsystem State" below. If genuinely new topic (rare), create a new subsystem struct.
2. **Add the field to the struct** and cover it in the struct's `reset()`.
3. **Add a handler fn** in the matching `*Handlers` namespace (in the subsystem `.cpp`). Signature: `void handleXX(SubsystemState &state, RadioState &owner, const QString &cmd)`. Mutate the struct, emit through the owner: `emit owner.myChanged(value);`.
4. **Expose a getter on RadioState** (`.h` file) that delegates: `Type myThing() const { return m_xxxState.myThing; }`.
5. **Declare the signal** on `RadioState` (signals still live on the façade since subsystems are not QObjects).
6. **Register the CAT prefix** in `RadioState::registerCommandHandlers()` — a one-line lambda that calls the handler fn.
7. **Add a test** in `tests/test_radiostate.cpp` covering the new prefix.
8. **Connect signal to UI** in whichever controller owns the widget (not MainWindow — see controller map in `docs/controllers.md`).

Rule: signals stay on `RadioState`; data and logic live in subsystems.

---

## Subsystem State

`RadioState` is the only `QObject` in the model layer. All radio state is partitioned across 9 plain-struct subsystems in `src/models/radiostate/`, each with a matching `*Handlers` namespace of free functions that mutate the struct and emit via the façade.

### Subsystem inventory

| Subsystem | Topic | CAT commands (primary) |
|-----------|-------|------------------------|
| `FrequencyVfoState` | VFO A/B frequencies, split, RIT/XIT offsets | FA, FB, FT, RT, RT$, XT, RO, RO$ |
| `ModeFilterState` | Operating mode, filter, CW keyer | MD, MD$, BW, BW$, IS, IS$, FP, FP$, CW, KS, KP |
| `ProcessingState` | NB / NR / PA / RA / GT / NA / NM (per VFO) | NB, NB$, NR, NR$, PA, PA$, RA, RA$, GT, GT$, NA, NA$, NM, NM$ |
| `AntennaState` | Antenna selection + per-band masks + ATU | AN, AT, ACN, ACM, ACS, ACT, AR, AR$ |
| `AudioEffectsState` | VOX, audio effects, APF, ESSB, EQ, line in/out, mic setup, monitor level, mix, balance | FX, AP, AP$, VX, VG, VI, ES, RE, TE, LO, LI, MI, MS, ML, MX, BL |
| `DataControlState` | Data sub-mode, rate, tuning step, streaming latency | DT, DT$, DR, DR$, TD, TD$, TB, TB$, VT, VT$, SL |
| `SpectrumDisplayState` | All `#`-prefix panadapter/waterfall display state | #REF, #SPN, #SCL, #MP, #DPM, #DSM, #FPS, #WFC, #WFH, #AVG, #PKM, #FXT, #FXA, #FRZ, #VFA, #VFB, #AR, #NB$, #NBL$ (plus EXT variants) |
| `TextDecodeState` | Text decoder mode / threshold / lines | TD, TD$, TB, TB$ |
| `RxTxMeterState` | S-meter, TX meter cluster, RX/TX transition, supply volts/amps, radio identity, message bank, SB/DV/TS/BS toggles | SM, SM$, PO, TM, TX, RX, ID, OM, RV., ER, MN, SIFP, SB, DV, TS, BS |

### Why plain structs + free functions (Pattern C)

- **Single `QObject` thread-affinity check.** Only `RadioState` is a `QObject`; subsystems can't accidentally sit on the wrong thread. `Q_ASSERT(QThread::currentThread() == thread())` in `parseCATCommand()` covers the whole hierarchy.
- **No moc storm.** Adding a subsystem doesn't touch the moc pipeline. No Q_OBJECT, no signals, no child-destructor ordering traps during RadioState teardown.
- **Public API unchanged.** External callers (CatServer, controllers, MainWindow, etc.) still see `radioState->frequency()`, `radioState->mode()`, etc. Subsystems are an implementation detail.
- **Signals still rollup via the façade.** When a handler fn mutates a field, it emits through the `owner` reference: `emit owner.frequencyChanged(...)`. Cross-cutting rollup signals (`processingChanged`, `antennaChanged`) emit from the same place they always did.

### File layout per subsystem

```
src/models/radiostate/xxxstate.h    // struct XxxState { ... void reset(); };
                                     // namespace XxxHandlers { handleXX(...); setXX(...); }
src/models/radiostate/xxxstate.cpp   // handler + setter bodies
```

Façade (`radiostate.cpp`) holds one pass-through line per handler:

```cpp
void RadioState::handleMD(const QString &cmd) {
    ModeFilterHandlers::handleMD(m_modeFilterState, *this, cmd);
}
```

### Invariants enforced by CI

- **Golden CAT-trace replay** (`test_radiostate_golden`) — every subsystem move must preserve the emit sequence byte-for-byte.
- **Registry invariant** (`test_radiostate_registry`) — every CAT prefix resolves, longest-first ordering preserved.
- **Signal-graph drift** (`tools/check_signal_graph_drift.py`) — per-file emit counts frozen in `docs/generated/baseline/`; regressions surface on PR.

### Adding a Protocol Packet Type

1. **network/protocol.h**: Add to `PayloadType` enum
2. **network/protocol.cpp**: Add case in `processPacket()`
3. **network/protocol.h**: Add signal for new packet type
4. **mainwindow.cpp**: Connect signal to handler

---

## Adding a Popup Menu

**All popup menus MUST inherit from `K4PopupBase`.**

### K4PopupBase Class Reference

Base class for all popup widgets. Defined in `src/ui/k4popupbase.h`.

**Inherits:** QWidget

**Inherited by (as of 2026-04-20):** AntennaCfgPopupWidget, BandPopupWidget, ButtonRowPopup, DisplayPopupWidget, FnPopupWidget, ModePopupWidget, RxEqPopupWidget. To regenerate this list: `rg '^class\s+\w+\s*:\s*public\s+K4PopupBase' src/ui`.

#### Public Methods

| Method | Description |
|--------|-------------|
| `K4PopupBase(QWidget *parent)` | Constructor. Sets up window flags, translucent background, focus policy |
| `showAboveButton(QWidget *trigger)` | Position and show popup centered above trigger button's parent |
| `showAboveWidget(QWidget *ref)` | Position and show popup above reference widget |
| `hidePopup()` | Hide popup and emit `closed()` signal |

#### Signals

| Signal | Description |
|--------|-------------|
| `closed()` | Emitted when popup is hidden (via hidePopup(), Escape key, or click outside) |

#### Protected Methods (for subclasses)

| Method | Description |
|--------|-------------|
| `contentSize()` | **Pure virtual.** Return QSize of content area (excluding shadow margins) |
| `contentMargins()` | Returns QMargins for layout (shadow + content padding) |
| `contentRect()` | Returns QRect of content area for custom painting |
| `initPopup()` | Sets widget size from contentSize(). **Must call at end of constructor** |
| `paintContent(QPainter&, QRect&)` | Override for custom painting after background/shadow |

#### What K4PopupBase Handles Automatically

- Window flags (`Qt::Popup | Qt::FramelessWindowHint`)
- Translucent background for shadow rendering
- Drop shadow drawing (8-layer blur via `K4Styles::drawDropShadow`)
- Popup background fill (`K4Styles::Colors::PopupBackground`)
- Escape key closes popup
- Screen boundary detection (keeps popup on-screen)
- `closed()` signal emission on hide

#### Implementation Details

```cpp
// Constructor sets up:
setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
setAttribute(Qt::WA_TranslucentBackground);
setFocusPolicy(Qt::StrongFocus);

// contentMargins() returns:
QMargins(ShadowMargin + PopupContentMargin, ...)  // All four sides

// initPopup() calculates:
totalSize = contentSize() + 2 * ShadowMargin
```

---

### Popup Creation Guide

K4PopupBase provides:
- Window flags and frameless behavior
- Drop shadow rendering (8-layer blur)
- Positioning above trigger buttons
- `closed()` signal on hide
- Escape key handling

### Template Files

| Popup Type | Template | Copy From |
|------------|----------|-----------|
| Grid with selection | `bandpopupwidget.cpp` | Multi-row buttons |
| Single row | `buttonrowpopup.cpp` | Simplest example |
| Settings panel | `displaypopupwidget.cpp` | +/- controls, toggles |
| Mode grid | `modepopupwidget.cpp` | Sub-mode logic |

### Step 1: Create Header File

```cpp
// src/ui/mypopup.h
#ifndef MYPOPUP_H
#define MYPOPUP_H

#include "k4popupbase.h"  // REQUIRED base class
#include <QPushButton>
#include <QList>

class MyPopup : public K4PopupBase {  // Inherit from K4PopupBase
    Q_OBJECT

public:
    explicit MyPopup(QWidget *parent = nullptr);

signals:
    void itemSelected(int index);
    // Note: closed() is inherited from K4PopupBase

protected:
    QSize contentSize() const override;  // REQUIRED - pure virtual

private:
    void setupUi();
    QList<QPushButton *> m_buttons;
};

#endif // MYPOPUP_H
```

### Step 2: Create Implementation File

```cpp
// src/ui/mypopup.cpp
#include "mypopup.h"
#include "k4styles.h"
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace {
const int ButtonWidth = 70;
const int ButtonHeight = 44;
const int ButtonSpacing = 8;
}

MyPopup::MyPopup(QWidget *parent) : K4PopupBase(parent) {
    setupUi();
}

void MyPopup::setupUi() {
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(contentMargins());  // Use base class method
    mainLayout->setSpacing(0);

    auto *rowLayout = new QHBoxLayout();
    rowLayout->setSpacing(ButtonSpacing);

    // Create buttons using K4Styles
    for (int i = 0; i < 7; ++i) {
        auto *btn = new QPushButton(QString::number(i + 1), this);
        btn->setFixedSize(ButtonWidth, ButtonHeight);
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setStyleSheet(K4Styles::popupButtonNormal());
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            emit itemSelected(i);
            hidePopup();  // Inherited from K4PopupBase
        });
        m_buttons.append(btn);
        rowLayout->addWidget(btn);
    }
    mainLayout->addLayout(rowLayout);

    initPopup();  // REQUIRED - must be last in constructor/setupUi
}

QSize MyPopup::contentSize() const {
    // Calculate content dimensions (excluding shadow margins)
    int cm = K4Styles::Dimensions::PopupContentMargin;

    int width = 7 * ButtonWidth + 6 * ButtonSpacing + 2 * cm;
    int height = ButtonHeight + 2 * cm;
    return QSize(width, height);
}
```

### Step 3: Add to CMakeLists.txt

```cmake
# In SOURCES section:
src/ui/mypopup.cpp

# In HEADERS section:
src/ui/mypopup.h
```

### Step 4: Wire Up in MainWindow

```cpp
// mainwindow.h - Add member
MyPopup *m_myPopup;

// mainwindow.cpp - Create and connect
m_myPopup = new MyPopup(this);
connect(m_myPopup, &MyPopup::itemSelected, this, &MainWindow::onMyItemSelected);
connect(m_myPopup, &MyPopup::closed, this, [this]() {
    m_triggerButton->setStyleSheet(K4Styles::menuBarButton());  // Reset button
});

// Show popup when trigger button clicked
connect(m_triggerButton, &QPushButton::clicked, this, [this]() {
    m_myPopup->showAboveButton(m_triggerButton);
    m_triggerButton->setStyleSheet(K4Styles::menuBarButtonActive());
});
```

### Custom Painting (Optional)

If you need custom drawing beyond child widgets, override `paintContent()`:

```cpp
void MyPopup::paintContent(QPainter &painter, const QRect &contentRect) {
    // K4PopupBase already drew: shadow, background
    // Add your custom drawing here
    painter.setPen(K4Styles::borderColor());
    painter.drawLine(contentRect.left() + 10, contentRect.center().y(),
                     contentRect.right() - 10, contentRect.center().y());
}
```

### Button Selection Styling

```cpp
void MyPopup::updateButtonStyles() {
    for (auto *btn : m_buttons) {
        if (btn == m_selectedButton) {
            btn->setStyleSheet(K4Styles::popupButtonSelected());
        } else {
            btn->setStyleSheet(K4Styles::popupButtonNormal());
        }
    }
}
```

### Key Points

1. **Always inherit from `K4PopupBase`** - never `QWidget` directly
2. **Implement `contentSize()`** - returns size of content area (K4PopupBase adds shadow margins)
3. **Call `initPopup()`** - must be the last call in constructor
4. **Use `contentMargins()`** - for layout margins (handles shadow + content padding)
5. **Use `K4Styles::*`** - for all colors, gradients, and button styling
6. **Never implement shadow code** - K4PopupBase handles this

---

## Controller Pattern

Controllers live in `src/controllers/` and own a cohesive slice of the UI or domain (e.g., all popups, the status bar, the feature menu, K4 MEDF menu). A controller's job is to encapsulate **widgets + signal wiring + CAT dispatch + slice-specific state** for that slice.

**MainWindow coordinates controllers; it does not reach into them.** See Architecture Rule 2 in `CONVENTIONS.md`: controllers expose task-level APIs, never their owned objects.

### Structural template

```cpp
// src/controllers/mycontroller.h
#ifndef MYCONTROLLER_H
#define MYCONTROLLER_H

#include <QObject>
class RadioState;
class ConnectionController;
class SomeWidget;

class MyController : public QObject {
    Q_OBJECT

  public:
    explicit MyController(RadioState *radioState,
                          ConnectionController *connection,
                          QWidget *parentWidget,   // parent for owned widgets
                          QObject *parent = nullptr);
    ~MyController() override;

    // Task-level API only. No widget getters.
    void togglePopup(PopupId id);
    PopupId activePopup() const;

  signals:
    void popupClosed(PopupId id);

  private:
    RadioState *m_radioState;            // injected, not owned
    ConnectionController *m_connection;  // injected, not owned
    SomeWidget *m_ownedWidget = nullptr; // owned via Qt parent mechanism
};
#endif
```

### Constructor rules

1. **Dependencies come in as pointers, not owned.** Store but never `delete`. If you didn't `new` it, you don't `delete` it.
2. **Widget `parentWidget` is separate from `QObject* parent`.** Widgets need a QWidget parent so Qt's paint/layout system works; the controller's QObject parent is usually MainWindow for cleanup purposes. Often the same pointer, but passed separately to make the distinction explicit.
3. **Constructor injection only.** Never `RadioState::instance()` or singleton lookup.

### Destructor rules

1. **First statement: `disconnect(this);`** — Architecture Rule 11. Prevents queued signals arriving during partial destruction.
2. **Owned QObject children delete automatically via Qt parent ownership.** Don't manually `delete` widgets.

### Public-API rules

1. **Task-level operations only.** `popupManager->togglePopup(PopupId::Display)`, not `popupManager->displayPopup()` returning the widget.
2. **Read access via `const Type&`** (Architecture Rule 3). Never return non-const references to internal state.
3. **State changes surface as signals**, not mutable accessors.

### Signal-wiring rules

1. **Controllers wire their own signals in the constructor.** Don't pass references back to MainWindow so it can connect them for you.
2. **Prefer direct observation** — if a widget only needs a single RadioState property, see "Direct Observation" below. Controllers are for coordinated state across multiple widgets.
3. **Never daisy-chain signals A → B → C for pass-through value propagation.** Re-emit only when transforming the data.

### Testing

1. Each controller gets a unit test file at `tests/test_<controllername>.cpp` (see `tests/CMakeLists.txt` pattern).
2. Construct with a real `RadioState` (no mocks — see `tests/test_radiostate.cpp` for the style).
3. Verify public API + signal flow. Don't poke internal widgets.

---

## Direct Observation

When a widget needs to render a single RadioState property, let it **observe RadioState directly** instead of routing through MainWindow or a controller. Eliminates pointless middleman slots like `MainWindow::onFrequencyChanged` that just forward to `m_vfoA->setFrequency`.

```cpp
// The widget takes a RadioState pointer at construction and wires its own
// connect() calls. Lives entirely in src/ui/; no controller required.
class VFOWidget : public QWidget {
    Q_OBJECT
  public:
    explicit VFOWidget(RadioState *state, QWidget *parent = nullptr);

  private slots:
    void onFrequencyChanged(quint64 freq);

  private:
    RadioState *m_state;  // injected, not owned
};

VFOWidget::VFOWidget(RadioState *state, QWidget *parent)
    : QWidget(parent), m_state(state) {
    connect(m_state, &RadioState::frequencyChanged,
            this, &VFOWidget::onFrequencyChanged);
}
```

### When to use direct observation vs a controller

| Choose... | If the widget... |
|---|---|
| **Direct observation** | Consumes a single property (frequency, SWR, VFO cursor), no cross-widget coordination, no CAT dispatch logic. |
| **Controller** | Participates in coordinated state across multiple widgets (e.g., feature menu where toggling one feature updates 5 indicators), or triggers CAT commands, or requires mode-dependent behavior spanning several widgets. |

**Rule of thumb:** if the slot body is just `m_target->setValue(incoming)`, the widget should observe directly. The middleman is pure overhead.

---

## State Subsystem Pattern (RadioState internals)

`RadioState` is the single `QObject` external code depends on — MainWindow, CatServer, and every controller connect to its signals + call its getters. That public surface must stay stable during the Phase 1 refactor (see `docs/radiostate-catserver-api-contract.md`).

Internal state is reorganized into **plain-struct subsystems** that RadioState composes. This pattern is the target shape once Phase 1 lands; apply it to any new RadioState expansion going forward.

### Rules

1. **Subsystems are plain structs, not `QObject`.** Keeps RadioState as the only QObject — one thread affinity, one `Q_ASSERT(currentThread() == thread())`, no moc explosion, no child-destructor signal-ordering surprises.

2. **RadioState is the only emitter.** Subsystem handlers take `RadioState&` by reference and call `emit owner.xxxChanged()` via the facade. The subsystem doesn't even know Qt signals exist.

3. **Subsystems own their field subset.** Sentinel initialization lives with the field. `RadioState::reset()` delegates to each subsystem's `reset()`.

4. **Each subsystem registers its own handler fragments** into RadioState's registry. `RadioState::registerCommandHandlers()` becomes composition: it calls each subsystem's `registerHandlers(RegistryBuilder&)`.

5. **Public API (getters + signals) stays on `RadioState`.** External callers see a stable surface even as internals evolve. If a getter must be exposed, RadioState delegates — the subsystem stays private.

### Subsystem file layout

```
src/models/radiostate.{cpp,h}            <- facade: registry + public API
src/models/radiostate/frequencyvfostate.{cpp,h}
src/models/radiostate/modefilterstate.{cpp,h}
src/models/radiostate/processingstate.{cpp,h}
...
```

---

## Anti-patterns (banned going forward)

These are the shapes that created the 2026-04 god-object problem. **PRs reintroducing them should be rejected, including by self-review.**

### 1. New functional code in `MainWindow`

MainWindow's job is **window chrome + top-level controller coordination, nothing else**. New UI work lands in a controller (or as a direct-observation widget). A PR that adds a widget pointer, a setup method, or a business-logic slot to MainWindow is a regression to the god-object shape.

### 2. MainWindow as signal middleman

Slots like:
```cpp
void MainWindow::onFooChanged(int v) { m_fooLabel->setText(QString::number(v)); }
```
are pure overhead. The widget should observe RadioState directly. MainWindow's job is coordination, not plumbing.

### 3. Inline lambdas > 5 lines or > 5 `connect()` calls in one location

Extract to a named helper method. If the helper grows past ~30 lines and has cohesive scope, promote to a controller. 200-line inline lambda blocks in `setupUi()` are the pre-refactor pattern and must not come back.

### 4. Cross-controller reach-in

`controllerA->someGetter()->doThing()` is a circular-dependency smell. Controllers communicate via signals, not direct method calls against each other's internals. Exception: a controller legitimately calling a task-level method on another controller via injection (e.g., `m_connection->sendCAT(...)` from any controller).

### 5. Hidden state in globals or singletons

All shared state flows through `RadioState` (or `ConnectionController` for network state). Controllers take what they need via constructor injection. No static service locators.

### 6. Bare `TODO` / `FIXME` / `HACK`

See CONVENTIONS.md → Comment Conventions. Either `// TODO(gh#NNN): <summary>` with a filed issue or delete the comment.
