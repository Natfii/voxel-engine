#pragma once

class Crosshair {
public:
    Crosshair();
    void render();
    void setVisible(bool visible);
    bool isVisible() const;

private:
    bool visible;
    float size;
    float thickness;
    float gap;
};
