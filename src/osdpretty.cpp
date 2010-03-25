/* This file is part of Clementine.

   Clementine is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Clementine is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Clementine.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "osdpretty.h"

#include <QColor>
#include <QPainter>
#include <QLayout>
#include <QApplication>
#include <QDesktopWidget>
#include <QSettings>
#include <QMouseEvent>
#include <QTimer>

#include <QtDebug>

const char* OSDPretty::kSettingsGroup = "OSDPretty";

const int OSDPretty::kDropShadowSize = 13;
const int OSDPretty::kBorderRadius = 10;
const int OSDPretty::kMaxIconSize = 100;

const QRgb OSDPretty::kPresetBlue = qRgb(102, 150, 227);
const QRgb OSDPretty::kPresetOrange = qRgb(254, 156, 67);

OSDPretty::OSDPretty(QWidget *parent)
  : QWidget(parent),
    mode_(Mode_Popup),
    background_color_(kPresetOrange),
    background_opacity_(0.85),
    popup_display_(0),
    timeout_(new QTimer(this))
{
  setWindowFlags(Qt::ToolTip |
                 Qt::FramelessWindowHint |
                 Qt::WindowStaysOnTopHint);
  setAttribute(Qt::WA_TranslucentBackground, true);
  ui_.setupUi(this);
  SetMode(mode_);

  timeout_->setSingleShot(true);
  timeout_->setInterval(5000);
  connect(timeout_, SIGNAL(timeout()), SLOT(hide()));

  ui_.icon->setMaximumSize(kMaxIconSize, kMaxIconSize);

  // Load the show edges and corners
  QImage shadow_edge(":osd_shadow_edge.png");
  QImage shadow_corner(":osd_shadow_corner.png");
  for (int i=0 ; i<4 ; ++i) {
    QTransform rotation = QTransform().rotate(90 * i);
    shadow_edge_[i] = QPixmap::fromImage(shadow_edge.transformed(rotation));
    shadow_corner_[i] = QPixmap::fromImage(shadow_corner.transformed(rotation));
  }

  // Set the margins to allow for the drop shadow
  int margin = layout()->contentsMargins().left() + kDropShadowSize;
  layout()->setContentsMargins(margin, margin, margin, margin);

  Load();
}

void OSDPretty::Load() {
  QSettings s;
  s.beginGroup(kSettingsGroup);

  foreground_color_ = QColor(s.value("foreground_color", 0).toInt());
  background_color_ = QColor(s.value("background_color", kPresetBlue).toInt());
  background_opacity_ = s.value("background_opacity", 0.85).toReal();
  popup_display_ = s.value("popup_display", -1).toInt();
  popup_pos_ = s.value("popup_pos", QPoint(0, 0)).toPoint();

  set_foreground_color(foreground_color());
}

void OSDPretty::ReloadSettings() {
  Load();
  if (isVisible())
    update();
}

void OSDPretty::SetMode(Mode mode) {
  mode_ = mode;

  switch (mode_) {
  case Mode_Popup:
    setCursor(QCursor(Qt::ArrowCursor));
    break;

  case Mode_Draggable:
    setCursor(QCursor(Qt::OpenHandCursor));
    break;
  }
}

void OSDPretty::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);
  p.setRenderHint(QPainter::HighQualityAntialiasing);

  QRect box(rect().adjusted(kDropShadowSize, kDropShadowSize, -kDropShadowSize, -kDropShadowSize));

  // Shadow corners
  const int kShadowCornerSize = kDropShadowSize + kBorderRadius;
  p.drawPixmap(0, 0, shadow_corner_[0]);
  p.drawPixmap(width() - kShadowCornerSize, 0, shadow_corner_[1]);
  p.drawPixmap(width() - kShadowCornerSize, height() - kShadowCornerSize, shadow_corner_[2]);
  p.drawPixmap(0, height() - kShadowCornerSize, shadow_corner_[3]);

  // Shadow edges
  p.drawTiledPixmap(kShadowCornerSize, 0,
                    width() - kShadowCornerSize*2, kDropShadowSize,
                    shadow_edge_[0]);
  p.drawTiledPixmap(width() - kDropShadowSize, kShadowCornerSize,
                    kDropShadowSize, height() - kShadowCornerSize*2,
                    shadow_edge_[1]);
  p.drawTiledPixmap(kShadowCornerSize, height() - kDropShadowSize,
                    width() - kShadowCornerSize*2, kDropShadowSize,
                    shadow_edge_[2]);
  p.drawTiledPixmap(0, kShadowCornerSize,
                    kDropShadowSize, height() - kShadowCornerSize*2,
                    shadow_edge_[3]);

  // Box background
  p.setBrush(background_color_);
  p.setPen(QPen());
  p.setOpacity(background_opacity_);
  p.drawRoundedRect(box, kBorderRadius, kBorderRadius);

  // Gradient overlay
  QLinearGradient gradient(0, 0, 0, height());
  gradient.setColorAt(0, QColor(255, 255, 255, 130));
  gradient.setColorAt(1, QColor(255, 255, 255, 50));
  p.setBrush(gradient);
  p.setOpacity(1.0);
  p.drawRoundedRect(box, kBorderRadius, kBorderRadius);

  // Box border
  p.setBrush(QBrush());
  p.setPen(QPen(background_color_.darker(150), 2));
  p.drawRoundedRect(box, kBorderRadius, kBorderRadius);
}

void OSDPretty::SetMessage(const QString& summary, const QString& message,
                           const QImage& image) {

  if (!image.isNull()) {
    QImage scaled_image =
        image.scaled(kMaxIconSize, kMaxIconSize,
                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui_.icon->setPixmap(QPixmap::fromImage(scaled_image));
    ui_.icon->show();
  } else {
    ui_.icon->hide();
  }

  ui_.summary->setText(summary);
  ui_.message->setText(message);

  if (isVisible())
    Reposition();

  if (isVisible() && mode_ == Mode_Popup)
    timeout_->start(); // Restart the timer
}

void OSDPretty::showEvent(QShowEvent* e) {
  QWidget::showEvent(e);

  Reposition();
  setWindowOpacity(1.0);

  if (mode_ == Mode_Popup)
    timeout_->start();
}

void OSDPretty::Reposition() {
  QDesktopWidget* desktop = QApplication::desktop();

  layout()->activate();
  resize(sizeHint());

  int screen = popup_display_ >= desktop->screenCount() ? -1 : popup_display_;
  QRect geometry(desktop->availableGeometry(screen));

  int x = popup_pos_.x() + geometry.left();
  int y = popup_pos_.y() + geometry.top();

  move(qBound(0, x, geometry.right() - width()),
       qBound(0, y, geometry.bottom() - height()));
}

void OSDPretty::enterEvent(QEvent *) {
  if (mode_ == Mode_Popup)
    setWindowOpacity(0.25);
}

void OSDPretty::leaveEvent(QEvent *) {
  setWindowOpacity(1.0);
}

void OSDPretty::mousePressEvent(QMouseEvent* e) {
  if (mode_ == Mode_Popup)
    hide();
  else {
    original_window_pos_ = pos();
    drag_start_pos_ = e->globalPos();
  }
}

void OSDPretty::mouseMoveEvent(QMouseEvent* e) {
  if (mode_ == Mode_Draggable) {
    QPoint delta = e->globalPos() - drag_start_pos_;
    QPoint new_pos = original_window_pos_ + delta;

    // Keep it to the bounds of the desktop
    QDesktopWidget* desktop = QApplication::desktop();
    QRect geometry(desktop->availableGeometry(e->globalPos()));

    new_pos.setX(qBound(geometry.left(), new_pos.x(), geometry.right() - width()));
    new_pos.setY(qBound(geometry.top(), new_pos.y(), geometry.bottom() - height()));

    move(new_pos);
  }
}

QPoint OSDPretty::current_pos() const {
  QDesktopWidget* desktop = QApplication::desktop();
  QRect geometry(desktop->availableGeometry(current_display()));

  return QPoint(pos().x() - geometry.left(),
                pos().y() - geometry.top());
}

int OSDPretty::current_display() const {
  QDesktopWidget* desktop = QApplication::desktop();
  return desktop->screenNumber(pos());
}

void OSDPretty::set_background_color(QRgb color) {
  background_color_ = color;
  if (isVisible())
    update();
}

void OSDPretty::set_background_opacity(qreal opacity) {
  background_opacity_ = opacity;
  if (isVisible())
    update();
}

void OSDPretty::set_foreground_color(QRgb color) {
  foreground_color_ = QColor(color);

  QPalette p;
  p.setColor(QPalette::WindowText, foreground_color_);

  ui_.summary->setPalette(p);
  ui_.message->setPalette(p);
}

void OSDPretty::set_popup_duration(int msec) {
  timeout_->setInterval(msec);
}
