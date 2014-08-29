#define _USE_MATH_DEFINES 1
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>
#include <GLES2/gl2.h>

typedef GLfloat Matrix_t[16];

/// Since GLES2 doesn't have all the nifty matrix transform functions that GL
/// has, we emulate some of them here for the sake of sanity from:
/// http://www.opengl.org/wiki/GluPerspective_code
void glhFrustumf2(Matrix_t mat,
                  GLfloat left,
                  GLfloat right,
                  GLfloat bottom,
                  GLfloat top,
                  GLfloat znear,
                  GLfloat zfar);

void glhPerspectivef2(Matrix_t mat,
                      GLfloat fovyInDegrees,
                      GLfloat aspectRatio,
                      GLfloat znear,
                      GLfloat zfar);

void identity_matrix(Matrix_t mat);
void multiply_matrix(const Matrix_t a, const Matrix_t b, Matrix_t mat);
void rotate_matrix(GLfloat x_deg, GLfloat y_deg, GLfloat z_deg, Matrix_t mat);
void translate_matrix(GLfloat x, GLfloat y, GLfloat z, Matrix_t mat);

#define deg_to_rad(x) (x * (M_PI / 180.0f))

void glhFrustumf2(Matrix_t mat,
                  GLfloat left,
                  GLfloat right,
                  GLfloat bottom,
                  GLfloat top,
                  GLfloat znear,
                  GLfloat zfar) {
  float temp, temp2, temp3, temp4;
  temp = 2.0f * znear;
  temp2 = right - left;
  temp3 = top - bottom;
  temp4 = zfar - znear;
  mat[0] = temp / temp2;
  mat[1] = 0.0f;
  mat[2] = 0.0f;
  mat[3] = 0.0f;
  mat[4] = 0.0f;
  mat[5] = temp / temp3;
  mat[6] = 0.0f;
  mat[7] = 0.0f;
  mat[8] = (right + left) / temp2;
  mat[9] = (top + bottom) / temp3;
  mat[10] = (-zfar - znear) / temp4;
  mat[11] = -1.0f;
  mat[12] = 0.0f;
  mat[13] = 0.0f;
  mat[14] = (-temp * zfar) / temp4;
  mat[15] = 0.0f;
}

void glhPerspectivef2(Matrix_t mat,
                      GLfloat fovyInDegrees,
                      GLfloat aspectRatio,
                      GLfloat znear,
                      GLfloat zfar) {
  float ymax, xmax;
  ymax = znear * tanf(fovyInDegrees * 3.14f / 360.0f);
  xmax = ymax * aspectRatio;
  glhFrustumf2(mat, -xmax, xmax, -ymax, ymax, znear, zfar);
}

void identity_matrix(Matrix_t mat) {
  memset(mat, 0, sizeof(Matrix_t));
  mat[0] = 1.0;
  mat[5] = 1.0;
  mat[10] = 1.0;
  mat[15] = 1.0;
}

void multiply_matrix(const Matrix_t a, const Matrix_t b, Matrix_t mat) {
  // Generate to a temporary first in case the output matrix and input
  // matrix are the same.
  Matrix_t out;

  out[0] = a[0] * b[0] + a[4] * b[1] + a[8] * b[2] + a[12] * b[3];
  out[1] = a[1] * b[0] + a[5] * b[1] + a[9] * b[2] + a[13] * b[3];
  out[2] = a[2] * b[0] + a[6] * b[1] + a[10] * b[2] + a[14] * b[3];
  out[3] = a[3] * b[0] + a[7] * b[1] + a[11] * b[2] + a[15] * b[3];

  out[4] = a[0] * b[4] + a[4] * b[5] + a[8] * b[6] + a[12] * b[7];
  out[5] = a[1] * b[4] + a[5] * b[5] + a[9] * b[6] + a[13] * b[7];
  out[6] = a[2] * b[4] + a[6] * b[5] + a[10] * b[6] + a[14] * b[7];
  out[7] = a[3] * b[4] + a[7] * b[5] + a[11] * b[6] + a[15] * b[7];

  out[8] = a[0] * b[8] + a[4] * b[9] + a[8] * b[10] + a[12] * b[11];
  out[9] = a[1] * b[8] + a[5] * b[9] + a[9] * b[10] + a[13] * b[11];
  out[10] = a[2] * b[8] + a[6] * b[9] + a[10] * b[10] + a[14] * b[11];
  out[11] = a[3] * b[8] + a[7] * b[9] + a[11] * b[10] + a[15] * b[11];

  out[12] = a[0] * b[12] + a[4] * b[13] + a[8] * b[14] + a[12] * b[15];
  out[13] = a[1] * b[12] + a[5] * b[13] + a[9] * b[14] + a[13] * b[15];
  out[14] = a[2] * b[12] + a[6] * b[13] + a[10] * b[14] + a[14] * b[15];
  out[15] = a[3] * b[12] + a[7] * b[13] + a[11] * b[14] + a[15] * b[15];

  memcpy(mat, out, sizeof(Matrix_t));
}

void rotate_x_matrix(GLfloat x_rad, Matrix_t mat) {
  identity_matrix(mat);
  mat[5] = cosf(x_rad);
  mat[6] = -sinf(x_rad);
  mat[9] = -mat[6];
  mat[10] = mat[5];
}

void rotate_y_matrix(GLfloat y_rad, Matrix_t mat) {
  identity_matrix(mat);
  mat[0] = cosf(y_rad);
  mat[2] = sinf(y_rad);
  mat[8] = -mat[2];
  mat[10] = mat[0];
}

void rotate_z_matrix(GLfloat z_rad, Matrix_t mat) {
  identity_matrix(mat);
  mat[0] = cosf(z_rad);
  mat[1] = sinf(z_rad);
  mat[4] = -mat[1];
  mat[5] = mat[0];
}

void rotate_matrix(GLfloat x_deg, GLfloat y_deg, GLfloat z_deg, Matrix_t mat) {
  GLfloat x_rad = (GLfloat) deg_to_rad(x_deg);
  GLfloat y_rad = (GLfloat) deg_to_rad(y_deg);
  GLfloat z_rad = (GLfloat) deg_to_rad(z_deg);

  Matrix_t x_matrix;
  Matrix_t y_matrix;
  Matrix_t z_matrix;

  rotate_x_matrix(x_rad, x_matrix);
  rotate_y_matrix(y_rad, y_matrix);
  rotate_z_matrix(z_rad, z_matrix);

  Matrix_t xy_matrix;
  multiply_matrix(y_matrix, x_matrix, xy_matrix);
  multiply_matrix(z_matrix, xy_matrix, mat);
}

void translate_matrix(GLfloat x, GLfloat y, GLfloat z, Matrix_t mat) {
  identity_matrix(mat);
  mat[12] += x;
  mat[13] += y;
  mat[14] += z;
}


const uint8_t kRLETextureData[] =
  "/wD/AP8A/w8ACf8SAAn/AP9ZAAP/CQAD/wwAA/8JAAP/AP9WAAP/CQAD/wwAA/8JAAP/AP9WAAP/"
  "CQAD/wwAA/8JAAP/AP9WAAP/CQAD/wwAA/8JAAP/AP9WAAP/CQAD/wMABv8DAAP/CQAD/wD/WQAJ"
  "/wYABv8GAAn/AP9uAAP/AP96AAP/AP8A/x8AA/8A/3oACf8A/3QAD/8A/24AFf8A/2gAG/8A/2IA"
  "If8A/1wAJ/8A/1YALf8A/1AAM/8A/0oAOf8A/0QAP/8A/z4ARf8A/zgAS/8A/zIAUf8A/ywAV/8A"
  "/yYAXf8A/yAAY/8A/xoAaf8A/xQAb/8A/w4Adf8A/wgAe/8A/wIADP8JAAP/CQAb/wYAD/8GACr/"
  "/AAS/wMACf8DAAkDAwQDABL/AwAS/wMALf/2ABX/AwAJ/wMABikD6AP8A78DGAMADP8DABL/AwAM"
  "EwOzA+YDsgMSAwAV//AAGP8PAAa2AykDAgM7A68DAAz/AwAS/wMADKUDSwMAA0sDpAMAGP/qABv/"
  "AwAJ/wMABvYD/wn1AwAM/wMAEv8DAAzzAwgDAAMIA/IDABv/5AAe/wMACf8DAAbDAwAY/wMAEv8D"
  "AAzzAwgDAAMIA/IDAB7/3gAh/wMACf8DAAaIA1IDDgMAEv8DABL/AwAMpgNTAwADUwOkAwAh/9gA"
  "If8JAAP/CQADDQOrA/gD2AN/AwAG/w8ABv8PAAYUA7kD8AO3AxIDACT/0gCx/8wAt//GAL3/wADD"
  "/7oAyf+0AM//rgA2/wkAA/8JADD/BgAV/wYAOf+oADkFA/YDAAYBA/cDAwMAM/8DABj/AwA8/6IA"
  "P+sDEAMAAxQD6AMABhMDswPmA7IDEgMABv8GKQPWA5UDAAz/AwAMHQPMA+gDRAP/AwA//5wAQtMD"
  "OQP/AzwD0AMABqUDSwMAA0sDpAMACf8DswMrA1oDAAz/AwAMqgNhAwYDXwP/AwBC/5YARboDcgP/"
  "A3QDuAMABvMDCAMAAwgD8gMACf8DCgMAEv8DAAzxAwkDAAMLA/8DAEX/kABIogOqA/8DrAOgAwAG"
  "8wMIAwADCAPyAwAJ/wMAFf8DAAzyAwkDAAMLA/8DAEj/igBLiQPjA/8D5AOIAwAGpgNTAwADUwOk"
  "AwAJ/wMAFf8DAAysA18DBQNfA/8DAEv/hABOcAP/A9oD/wNwAwAGFAO5A/ADtwMSAwAG/w8ABv8P"
  "AAYeA8wD6ANFA/8GAEv/fgAAAAX/eAAAAAv/cgAAABH/bAAAABf/ZgAAAB3/YAAAACP/vQBm/wD/"
  "GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm"
  "/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/"
  "GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm"
  "/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/"
  "GgBm/wD/GgBm/wD/GgBm/wD/GgBm/wD/GgBm/x8AAv8BAAL/AQAC/wEAAv8BAAL/AwAB/wEAAv8B"
  "AAL/AQAC/wEAAv8BAAH/AwAC/wEAAv8BAAL/AQAC/wEAAv/KAGb/HwAC/wEAAv8BAAL/AQAC/wEA"
  "Av8DAAH/AQAC/wEAAv8BAAL/AQAC/wEAAf8DAAL/AQAC/wEAAv8BAAL/AQAC/8oAZv8fAAL/AQAC"
  "/wEAAv8BAAL/AQAC/wMAAf8BAAL/AQAC/wEAAv8BAAL/AQAB/wMAAv8BAAL/AQAC/wEAAv8BAAL/"
  "ygBm/x8AAv8BAAL/AQAC/wEAAv8BAAL/AwAB/wEAAv8BAAL/AQAC/wEAAv8BAAH/AwAC/wEAAv8B"
  "AAL/AQAC/wEAAv/KAGb/HwAC/wEAAv8BAAL/AQAC/wEAAv8DAAH/AQAC/wEAAv8BAAL/AQAC/wEA"
  "Af8DAAL/AQAC/wEAAv8BAAL/AQAC/8oAZv8fAAL/AQAC/wEAAv8BAAL/AQAC/wMAAf8BAAL/AQAC"
  "/wEAAv8BAAL/AQAB/wMAAv8BAAL/AQAC/wEAAv8BAAL/ygBm/x8AAv8BAAL/AQAC/wEAAv8BAAL/"
  "AwAB/wEAAv8BAAL/AQAC/wEAAv8BAAH/AwAC/wEAAv8BAAL/AQAC/wEAAv/KAGb/HwAC/wEAAv8B"
  "AAL/AQAC/wEAAv8DAAH/AQAC/wEAAv8BAAL/AQAC/wEAAf8DAAL/AQAC/wEAAv8BAAL/AQAC/8oA"
  "Zv8fAAL/AQAC/wEAAv8BAAL/AQAC/wMAAf8BAAL/AQAC/wEAAv8BAAL/AQAB/wMAAv8BAAL/AQAC"
  "/wEAAv8BAAL/ygBm/x8AAv8BAAL/AQAC/wEAAv8BAAL/AwAB/wEAAv8BAAL/AQAC/wEAAv8BAAH/"
  "AwAC/wEAAv8BAAL/AQAC/wEAAv/KAGb/HwAC/wEAAv8BAAL/AQAC/wEAAv8DAAH/AQAC/wEAAv8B"
  "AAL/AQAC/wEAAf8DAAL/AQAC/wEAAv8BAAL/AQAC/8oAZv8fAAL/AQAC/wEAAv8BAAL/AQAC/wMA"
  "Af8BAAL/AQAC/wEAAv8BAAL/AQAB/wMAAv8BAAL/AQAC/wEAAv8BAAL/ygBm/x8AAv8BAAL/AQAC"
  "/wEAAv8BAAL/AwAB/wEAAv8BAAL/AQAC/wEAAv8BAAH/AwAC/wEAAv8BAAL/AQAC/wEAAv/KAGb/"
  "HwAC/wEAAv8BAAL/AQAC/wEAAv8DAAH/AQAC/wEAAv8BAAL/AQAC/wEAAf8DAAL/AQAC/wEAAv8B"
  "AAL/AQAC/8oAZv8fAAL/AQAC/wEAAv8BAAL/AQAC/wMAAf8BAAL/AQAC/wEAAv8BAAL/AQAB/wMA"
  "Av8BAAL/AQAC/wEAAv8BAAL/ygBm/x8AAv8BAAL/AQAC/wEAAv8BAAL/AwAB/wEAAv8BAAL/AQAC"
  "/wEAAv8BAAH/AwAC/wEAAv8BAAL/AQAC/wEAAv/KAGb/HwAC/wEAAv8BAAL/AQAC/wEAAv8DAAH/"
  "AQAC/wEAAv8BAAL/AQAC/wEAAf8DAAL/AQAC/wEAAv8BAAL/AQAC/8oAZv8fAAL/AQAC/wEAAv8B"
  "AAL/AQAC/wMAAf8BAAL/AQAC/wEAAv8BAAL/AQAB/wMAAv8BAAL/AQAC/wEAAv8BAAL/ygBm/x8A"
  "Av8BAAL/AQAC/wEAAv8BAAL/AwAB/wEAAv8BAAL/AQAC/wEAAv8BAAH/AwAC/wEAAv8BAAL/AQAC"
  "/wEAAv/KAGb/HwAC/wEAAv8BAAL/AQAC/wEAAv8DAAH/AQAC/wEAAv8BAAL/AQAC/wEAAf8DAAL/"
  "AQAC/wEAAv8BAAL/AQAC/8oAZv8fAAL/AQAC/wEAAv8BAAL/AQAC/wMAAf8BAAL/AQAC/wEAAv8B"
  "AAL/AQAB/wMAAv8BAAL/AQAC/wEAAv8BAAL/ygBm/x8AAv8BAAL/AQAC/wEAAv8BAAL/AwAB/wEA"
  "Av8BAAL/AQAC/wEAAv8BAAH/AwAC/wEAAv8BAAL/AQAC/wEAAv/KAGb/HwAC/wEAAv8BAAL/AQAC"
  "/wEAAv8DAAH/AQAC/wEAAv8BAAL/AQAC/wEAAf8DAAL/AQAC/wEAAv8BAAL/AQAC/8oAMzwDADD/"
  "HwAC/wEAAv8BAAL/AQAC/wEAAv8DAAH/AQAC/wEAAv8BAAL/AQAC/wEAAf8DAAL/AQAC/wEAAv8B"
  "AAL/AQAC/8oAZv8fAAL/AQAC/wEAAv8BAAL/AQAC/wMAAf8BAAL/AQAC/wEAAv8BAAL/AQAB/wMA"
  "Av8BAAL/AQAC/wEAAv8BAAL/Og==";

const size_t kRLETextureDataLength = sizeof(kRLETextureData) - 1;

const uint8_t kBase64Decode[256] = {
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0, 62,  0,  0,  0, 63,
   52, 53, 54, 55, 56, 57, 58, 59, 60, 61,  0,  0,  0,  0,  0,  0,
    0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
   15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,  0,  0,  0,  0,  0,
    0, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
   41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51,
  };
#if 0
/*Shaders YUV*/
  static const char kVertexShader[] =
      "varying vec2 v_texCoord;            \n"
      "attribute vec4 a_position;          \n"
      "attribute vec2 a_texCoord;          \n"
      "void main()                         \n"
      "{                                   \n"
      "    v_texCoord = a_texCoord;        \n"
      "    gl_Position = a_position;       \n"
      "}";

  static const char kFragmentShader[] =
      "precision mediump float;                                   \n"
      "varying vec2 v_texCoord;                                   \n"
      "uniform sampler2D y_texture;                               \n"
      "uniform sampler2D u_texture;                               \n"
      "uniform sampler2D v_texture;                               \n"
      "uniform mat3 color_matrix;                                 \n"
      "void main()                                                \n"
      "{                                                          \n"
      "  vec3 yuv;                                                \n"
      "  yuv.x = texture2D(y_texture, v_texCoord).r;              \n"
      "  yuv.y = texture2D(u_texture, v_texCoord).r;              \n"
      "  yuv.z = texture2D(v_texture, v_texCoord).r;              \n"
      "  vec3 rgb = color_matrix * (yuv - vec3(0.0625, 0.5, 0.5));\n"
      "  gl_FragColor = vec4(rgb, 1.0);                           \n"
      "}";

  static const float kColorMatrix[9] = {
    1.1643828125f, 1.1643828125f, 1.1643828125f,
    0.0f, -0.39176171875f, 2.017234375f,
    1.59602734375f, -0.81296875f, 0.0f
  };
#endif

const char kFragShaderSource[] =
    "precision mediump float;\n"
    "varying vec3 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D u_texture;\n"
    "void main() {\n"
    "  gl_FragColor = texture2D(u_texture, v_texcoord);\n"
    "  gl_FragColor += vec4(v_color, 1);\n"
    "}\n";

const char kVertexShaderSource[] =
    "uniform mat4 u_mvp;\n"
    "attribute vec2 a_texcoord;\n"
    "attribute vec3 a_color;\n"
    "attribute vec4 a_position;\n"
    "varying vec3 v_color;\n"
    "varying vec2 v_texcoord;\n"
    "void main() {\n"
    "  gl_Position = u_mvp * a_position;\n"
    "  v_color = a_color;\n"
    "  v_texcoord = a_texcoord;\n"
    "}\n";

struct Vertex {
  float loc[3];
  float color[3];
  float tex[2];
};


const struct Vertex kCubeVerts[24] = {
  // +Z (red arrow, black tip)
  {{-1.0, -1.0, +1.0}, {0.0, 0.0, 0.0}, {1.0, 0.0}},
  {{+1.0, -1.0, +1.0}, {0.0, 0.0, 0.0}, {0.0, 0.0}},
  {{+1.0, +1.0, +1.0}, {0.5, 0.0, 0.0}, {0.0, 1.0}},
  {{-1.0, +1.0, +1.0}, {0.5, 0.0, 0.0}, {1.0, 1.0}},

  // +X (green arrow, black tip)
  {{+1.0, -1.0, -1.0}, {0.0, 0.0, 0.0}, {1.0, 0.0}},
  {{+1.0, +1.0, -1.0}, {0.0, 0.0, 0.0}, {0.0, 0.0}},
  {{+1.0, +1.0, +1.0}, {0.0, 0.5, 0.0}, {0.0, 1.0}},
  {{+1.0, -1.0, +1.0}, {0.0, 0.5, 0.0}, {1.0, 1.0}},

  // +Y (blue arrow, black tip)
  {{-1.0, +1.0, -1.0}, {0.0, 0.0, 0.0}, {1.0, 0.0}},
  {{-1.0, +1.0, +1.0}, {0.0, 0.0, 0.0}, {0.0, 0.0}},
  {{+1.0, +1.0, +1.0}, {0.0, 0.0, 0.5}, {0.0, 1.0}},
  {{+1.0, +1.0, -1.0}, {0.0, 0.0, 0.5}, {1.0, 1.0}},

  // -Z (red arrow, red tip)
  {{+1.0, +1.0, -1.0}, {0.0, 0.0, 0.0}, {1.0, 1.0}},
  {{-1.0, +1.0, -1.0}, {0.0, 0.0, 0.0}, {0.0, 1.0}},
  {{-1.0, -1.0, -1.0}, {1.0, 0.0, 0.0}, {0.0, 0.0}},
  {{+1.0, -1.0, -1.0}, {1.0, 0.0, 0.0}, {1.0, 0.0}},

  // -X (green arrow, green tip)
  {{-1.0, +1.0, +1.0}, {0.0, 0.0, 0.0}, {1.0, 1.0}},
  {{-1.0, -1.0, +1.0}, {0.0, 0.0, 0.0}, {0.0, 1.0}},
  {{-1.0, -1.0, -1.0}, {0.0, 1.0, 0.0}, {0.0, 0.0}},
  {{-1.0, +1.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0}},

  // -Y (blue arrow, blue tip)
  {{+1.0, -1.0, +1.0}, {0.0, 0.0, 0.0}, {1.0, 1.0}},
  {{+1.0, -1.0, -1.0}, {0.0, 0.0, 0.0}, {0.0, 1.0}},
  {{-1.0, -1.0, -1.0}, {0.0, 0.0, 1.0}, {0.0, 0.0}},
  {{-1.0, -1.0, +1.0}, {0.0, 0.0, 1.0}, {1.0, 0.0}},
};

const GLubyte kCubeIndexes[36] = {
   2,  1,  0,  3,  2,  0,
   6,  5,  4,  7,  6,  4,
  10,  9,  8, 11, 10,  8,
  14, 13, 12, 15, 14, 12,
  18, 17, 16, 19, 18, 16,
  22, 21, 20, 23, 22, 20,
};
