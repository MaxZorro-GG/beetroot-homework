import processing.serial.*;

Serial myPort;
int iAngle = 90;
int iDistance = 0;

void setup() {
  size(1200, 700);
  smooth();
  printArray(Serial.list());
  myPort = new Serial(this, "COM7", 115200);
  myPort.bufferUntil('\n');
}

void draw() {
  noStroke();
  fill(0, 20);
  rect(0, 0, width, height);

  fill(98, 245, 31);
  drawRadar();
  drawLine();
  drawObject();
  drawHud();
}

void serialEvent(Serial p) {
  String line = p.readStringUntil('\n');
  if (line == null) return;
  line = trim(line);
  line = line.replace(".", "");
  int comma = line.indexOf(",");
  if (comma < 1) return;
  iAngle = int(line.substring(0, comma));
  iDistance = int(line.substring(comma + 1));
}

void drawRadar() {
  pushMatrix();
  translate(width / 2.0, height - 50);
  noFill();
  strokeWeight(2);
  stroke(98, 245, 31);
  arc(0, 0, 1000, 1000, PI, TWO_PI);
  arc(0, 0, 750, 750, PI, TWO_PI);
  arc(0, 0, 500, 500, PI, TWO_PI);
  arc(0, 0, 250, 250, PI, TWO_PI);
  line(-500, 0, 500, 0);
  for (int a = 30; a <= 150; a += 30) {
    line(0, 0, 500 * cos(radians(a)), -500 * sin(radians(a)));
  }
  popMatrix();
}

void drawLine() {
  pushMatrix();
  translate(width / 2.0, height - 50);
  strokeWeight(4);
  stroke(30, 250, 60);
  line(0, 0, 500 * cos(radians(iAngle)), -500 * sin(radians(iAngle)));
  popMatrix();
}

void drawObject() {
  if (iDistance <= 0 || iDistance > 40) return;
  pushMatrix();
  translate(width / 2.0, height - 50);
  float r = map(iDistance, 0, 40, 0, 500);
  strokeWeight(8);
  stroke(255, 10, 10);
  line(r * cos(radians(iAngle)), -r * sin(radians(iAngle)),
       500 * cos(radians(iAngle)), -500 * sin(radians(iAngle)));
  popMatrix();
}

void drawHud() {
  fill(0);
  noStroke();
  rect(0, height - 50, width, 50);
  fill(98, 245, 31);
  textSize(22);
  text("Angle: " + iAngle + " deg", 40, height - 18);
  if (iDistance > 0 && iDistance < 40) {
    text("Distance: " + iDistance + " cm", 320, height - 18);
  } else {
    text("Distance: out of range", 320, height - 18);
  }
}
