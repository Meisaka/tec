---
title: tec::AbstractWindow

---

# tec::AbstractWindow



Inherited by [tec::ActiveEntityTooltip](/engine/Classes/classtec_1_1_active_entity_tooltip/), [tec::Console](/engine/Classes/classtec_1_1_console/), [tec::DebugInfo](/engine/Classes/classtec_1_1_debug_info/), [tec::PingTimesWindow](/engine/Classes/classtec_1_1_ping_times_window/), [tec::ServerConnectWindow](/engine/Classes/classtec_1_1_server_connect_window/)

## Public Functions

|                | Name           |
| -------------- | -------------- |
| virtual | **[~AbstractWindow](/engine/Classes/classtec_1_1_abstract_window/#function-~abstractwindow)**() =default |
| virtual void | **[Update](/engine/Classes/classtec_1_1_abstract_window/#function-update)**(double ) |
| virtual void | **[Draw](/engine/Classes/classtec_1_1_abstract_window/#function-draw)**() =0 |
| void | **[SetAlpha](/engine/Classes/classtec_1_1_abstract_window/#function-setalpha)**(float a) |
| float | **[GetAlpha](/engine/Classes/classtec_1_1_abstract_window/#function-getalpha)**() const |
| virtual bool | **[isCollapsed](/engine/Classes/classtec_1_1_abstract_window/#function-iscollapsed)**() const |
| bool | **[isVisible](/engine/Classes/classtec_1_1_abstract_window/#function-isvisible)**() const |
| void | **[Hide](/engine/Classes/classtec_1_1_abstract_window/#function-hide)**() |
| void | **[Show](/engine/Classes/classtec_1_1_abstract_window/#function-show)**() |

## Protected Attributes

|                | Name           |
| -------------- | -------------- |
| float | **[alpha](/engine/Classes/classtec_1_1_abstract_window/#variable-alpha)**  |
| bool | **[collapsed](/engine/Classes/classtec_1_1_abstract_window/#variable-collapsed)**  |
| bool | **[show](/engine/Classes/classtec_1_1_abstract_window/#variable-show)** <br>Collapsed ?  |

## Public Functions Documentation

### function ~AbstractWindow

```cpp
virtual ~AbstractWindow() =default
```


### function Update

```cpp
inline virtual void Update(
    double 
)
```


**Reimplemented by**: [tec::Console::Update](/engine/Classes/classtec_1_1_console/#function-update), [tec::ActiveEntityTooltip::Update](/engine/Classes/classtec_1_1_active_entity_tooltip/#function-update), [tec::DebugInfo::Update](/engine/Classes/classtec_1_1_debug_info/#function-update), [tec::ServerConnectWindow::Update](/engine/Classes/classtec_1_1_server_connect_window/#function-update), [tec::PingTimesWindow::Update](/engine/Classes/classtec_1_1_ping_times_window/#function-update)


### function Draw

```cpp
virtual void Draw() =0
```


**Reimplemented by**: [tec::ActiveEntityTooltip::Draw](/engine/Classes/classtec_1_1_active_entity_tooltip/#function-draw), [tec::Console::Draw](/engine/Classes/classtec_1_1_console/#function-draw), [tec::DebugInfo::Draw](/engine/Classes/classtec_1_1_debug_info/#function-draw), [tec::ServerConnectWindow::Draw](/engine/Classes/classtec_1_1_server_connect_window/#function-draw), [tec::PingTimesWindow::Draw](/engine/Classes/classtec_1_1_ping_times_window/#function-draw)


### function SetAlpha

```cpp
inline void SetAlpha(
    float a
)
```


### function GetAlpha

```cpp
inline float GetAlpha() const
```


### function isCollapsed

```cpp
inline virtual bool isCollapsed() const
```


**Reimplemented by**: [tec::Console::isCollapsed](/engine/Classes/classtec_1_1_console/#function-iscollapsed)


### function isVisible

```cpp
inline bool isVisible() const
```


### function Hide

```cpp
inline void Hide()
```


### function Show

```cpp
inline void Show()
```


## Protected Attributes Documentation

### variable alpha

```cpp
float alpha = 1.0f;
```


### variable collapsed

```cpp
bool collapsed = true;
```


### variable show

```cpp
bool show = true;
```

Collapsed ? 

-------------------------------

Updated on 21 March 2021 at 16:58:09 UTC