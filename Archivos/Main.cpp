#define GLM_ENABLE_EXPERIMENTAL

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <vector>
#include <map>
#include <string>
#include <sstream>
#include <cstdlib>
#include <climits>
#include <cctype>
#include <assimp/config.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "stb_image.h"
#include "SOIL2/SOIL2.h"
#include "Shader.h"
#include "Camera.h"
#include "Model.h"
#include "Texture.h"
#include "Animation.h"
#include "Animator.h"

// Altura del terreno.
struct TrianguloTerreno { glm::vec3 a, b, c; };
std::vector<TrianguloTerreno> triangulosTerreno;

float escalaPiso = 1.0f;

void cargarAlturasTerreno()
{
    Assimp::Importer lector;
    const aiScene* escena = lector.ReadFile("ModelsExtra/Piso/Piso.fbx", aiProcess_Triangulate);
    if (!escena) { std::cout << "[TERRENO] No se pudo consultar Piso.fbx: " << lector.GetErrorString() << std::endl; return; }
    for (unsigned int m = 0; m < escena->mNumMeshes; ++m)
    {
        const aiMesh* mesh = escena->mMeshes[m];
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& cara = mesh->mFaces[f];
            if (cara.mNumIndices != 3) continue;
            glm::vec3 puntos[3];
            for (int i = 0; i < 3; ++i) {
                const aiVector3D& v = mesh->mVertices[cara.mIndices[i]];
                puntos[i] = glm::vec3(v.x, v.z, -v.y) * escalaPiso;
            }
            triangulosTerreno.push_back({ puntos[0],puntos[1],puntos[2] });
        }
    }
}

float alturaTerreno(float x, float z)
{
    float altura = -1e30f;
    for (const auto& t : triangulosTerreno)
    {
        float d = (t.b.z - t.c.z) * (t.a.x - t.c.x) + (t.c.x - t.b.x) * (t.a.z - t.c.z);
        if (std::abs(d) < 0.000001f) continue;
        float u = ((t.b.z - t.c.z) * (x - t.c.x) + (t.c.x - t.b.x) * (z - t.c.z)) / d;
        float v = ((t.c.z - t.a.z) * (x - t.c.x) + (t.a.x - t.c.x) * (z - t.c.z)) / d;
        if (u >= -0.00001f && v >= -0.00001f && u + v <= 1.00001f)
        {
            float y = u * t.a.y + v * t.b.y + (1 - u - v) * t.c.y;
            if (y > altura) altura = y;
        }
    }
    return altura > -1e29f ? altura : 0.0f;
}

// Modelos del escenario.
class ObjetoEscenario
{
    std::vector<Mesh> piezas;
    glm::vec3 minimo = glm::vec3(1e30f);
    glm::vec3 maximo = glm::vec3(-1e30f);
    std::string carpeta;
    std::string rutaModelo;
    std::string texturaPreferida;
    std::map<std::string, GLuint> texturas;

    std::vector<Mesh> cabezaPiezas;
    glm::vec3 pivoteCabeza = glm::vec3(0.0f);
    bool cabezaFrentePos = true;

    static std::string rutaNormal(std::string ruta)
    {
        for (char& c : ruta) if (c == '\\') c = '/';
        return ruta;
    }

    GLuint cargarTextura(aiMaterial* material, const aiScene* escena)
    {
        aiString nombre, nombreMaterial;
        material->Get(AI_MATKEY_NAME, nombreMaterial);
        std::string etiqueta = nombreMaterial.C_Str();
        bool tieneRuta = material->GetTexture(aiTextureType_DIFFUSE, 0, &nombre) == AI_SUCCESS;
        std::string ruta = tieneRuta ? rutaNormal(nombre.C_Str()) : "";
        std::string clave = ruta.empty() ? "material:" + etiqueta : ruta;
        auto guardada = texturas.find(clave);
        if (guardada != texturas.end()) return guardada->second;

        int ancho = 1, alto = 1;
        unsigned char* imagen = nullptr;
        std::vector<unsigned char> pixeles;
        if (!texturaPreferida.empty())
        {
            std::string externa = carpeta + "/" + texturaPreferida;
            imagen = SOIL_load_image(externa.c_str(), &ancho, &alto, nullptr, SOIL_LOAD_RGBA);
        }
        const aiTexture* interna = nullptr;
        if (!ruta.empty() && ruta[0] == '*')
        {
            char* fin = nullptr;
            unsigned long indice = std::strtoul(ruta.c_str() + 1, &fin, 10);
            if (fin != ruta.c_str() + 1 && *fin == '\0' && indice < escena->mNumTextures)
                interna = escena->mTextures[indice];
        }
        if (!imagen && interna && interna->pcData)
        {
            if (interna->mHeight == 0 && interna->mWidth <= INT_MAX)
                imagen = SOIL_load_image_from_memory(
                    reinterpret_cast<const unsigned char*>(interna->pcData),
                    static_cast<int>(interna->mWidth), &ancho, &alto, nullptr, SOIL_LOAD_RGBA);
            else if (interna->mHeight > 0 && interna->mWidth <= INT_MAX && interna->mHeight <= INT_MAX)
            {
                ancho = static_cast<int>(interna->mWidth);
                alto = static_cast<int>(interna->mHeight);
                size_t cantidad = static_cast<size_t>(ancho) * alto;
                pixeles.resize(cantidad * 4);
                for (size_t i = 0; i < cantidad; ++i)
                {
                    pixeles[i * 4] = interna->pcData[i].r;
                    pixeles[i * 4 + 1] = interna->pcData[i].g;
                    pixeles[i * 4 + 2] = interna->pcData[i].b;
                    pixeles[i * 4 + 3] = interna->pcData[i].a;
                }
            }
        }

        std::vector<std::string> candidatos;
        if (!ruta.empty() && ruta[0] != '*')
        {
            std::string archivo = ruta.substr(ruta.find_last_of('/') + 1);
            candidatos.push_back(carpeta + "/" + ruta);
            size_t fbm = ruta.find(".fbm/");
            if (fbm != std::string::npos)
            {
                size_t inicio = ruta.rfind('/', fbm);
                candidatos.push_back(carpeta + "/" + ruta.substr(
                    inicio == std::string::npos ? 0 : inicio + 1));
            }
            candidatos.push_back(carpeta + "/" + archivo);
            candidatos.push_back(carpeta + "/textures/" + archivo);
            candidatos.push_back(carpeta + "/Texturas/" + archivo);
        }

        std::string materialNormal = etiqueta;
        for (char& c : materialNormal) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        size_t sufijo = materialNormal.find('.');
        if (sufijo != std::string::npos) materialNormal.erase(sufijo);
        if (materialNormal == "adobe" || materialNormal == "madera" ||
            materialNormal == "teja" || materialNormal == "azul" || materialNormal == "goat")
        {
            candidatos.push_back(carpeta + "/" + materialNormal + ".png");
            candidatos.push_back(carpeta + "/Casa.fbm/" + materialNormal + ".png");
            candidatos.push_back(carpeta + "/textures/" + materialNormal + ".png");
        }
        if (!imagen && pixeles.empty())
        {
            for (const auto& candidato : candidatos)
            {
                std::ifstream comprobacion(candidato, std::ios::binary);
                if (!comprobacion.good()) continue;
                imagen = SOIL_load_image(candidato.c_str(), &ancho, &alto, nullptr, SOIL_LOAD_RGBA);
                if (imagen) break;
            }
        }

        aiColor3D color(0.75f, 0.75f, 0.75f);
        material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
        auto canal = [](float valor) -> unsigned char {
            if (valor < 0.0f) valor = 0.0f;
            if (valor > 1.0f) valor = 1.0f;
            return static_cast<unsigned char>(valor * 255.0f);
            };
        unsigned char pixel[] = { canal(color.r), canal(color.g), canal(color.b), 255 };
        const unsigned char* datos = imagen;
        if (!pixeles.empty()) datos = pixeles.data();
        if (!datos)
        {
            ancho = alto = 1;
            datos = pixel;
        }

        GLuint textura;
        glGenTextures(1, &textura);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, textura);
        GLint alineacion;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &alineacion);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ancho, alto, 0, GL_RGBA, GL_UNSIGNED_BYTE, datos);
        glPixelStorei(GL_UNPACK_ALIGNMENT, alineacion);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        if (imagen) SOIL_free_image_data(imagen);
        glBindTexture(GL_TEXTURE_2D, 0);
        texturas[clave] = textura;
        return textura;
    }

    glm::mat4 matrizColocacion(const glm::vec3& posicion, float altura, float giro) const
    {
        float altoOriginal = maximo.y - minimo.y;
        float escala = altura / altoOriginal;
        glm::vec3 apoyo((minimo.x + maximo.x) * 0.5f,
            minimo.y, (minimo.z + maximo.z) * 0.5f);
        glm::vec3 apoyoTerreno = posicion;
        float suelo = alturaTerreno(posicion.x, posicion.z);
        if (apoyoTerreno.y < suelo) apoyoTerreno.y = suelo;
        glm::mat4 matriz = glm::translate(glm::mat4(1.0f), apoyoTerreno);
        matriz = glm::rotate(matriz, glm::radians(giro), glm::vec3(0, 1, 0));
        matriz = glm::scale(matriz, glm::vec3(escala));
        matriz = glm::translate(matriz, -apoyo);
        return matriz;
    }

    bool ejeLargoEsZ() const { return (maximo.z - minimo.z) >= (maximo.x - minimo.x); }

    glm::mat4 cabeceoAlrededor(const glm::vec3& pivote, float grados,
        bool frentePositivo, bool bajarFrente) const
    {
        bool ejeZ = ejeLargoEsZ();
        float signo = (ejeZ ? 1.0f : -1.0f) * (frentePositivo ? 1.0f : -1.0f);
        float angulo = bajarFrente ? signo * grados : -signo * grados;
        glm::vec3 eje = ejeZ ? glm::vec3(1, 0, 0) : glm::vec3(0, 0, 1);
        glm::mat4 m = glm::translate(glm::mat4(1.0f), pivote);
        m = glm::rotate(m, glm::radians(angulo), eje);
        return glm::translate(m, -pivote);
    }

    void dibujarLista(Shader& shader, const glm::mat4& matriz, bool recortarAlpha,
        std::vector<Mesh>& lista)
    {
        glUniformMatrix4fv(glGetUniformLocation(shader.Program, "model"),
            1, GL_FALSE, glm::value_ptr(matriz));
        glUniform1i(glGetUniformLocation(shader.Program, "useBones"), 0);
        glUniform1i(glGetUniformLocation(shader.Program, "material.diffuse"), 0);
        glUniform1i(glGetUniformLocation(shader.Program, "material.specular"), 0);
        glUniform1i(glGetUniformLocation(shader.Program, "transparency"), recortarAlpha ? 1 : 0);
        for (auto& pieza : lista) pieza.Draw(shader);
        glUniform1i(glGetUniformLocation(shader.Program, "transparency"), 0);
    }

public:
    bool disponible = false;

    explicit ObjetoEscenario(const char* ruta, const char* texturaExterna = "")
    {
        std::string archivo = rutaNormal(ruta);
        rutaModelo = archivo;
        texturaPreferida = texturaExterna;
        std::ifstream prueba(archivo, std::ios::binary | std::ios::ate);
        if (!prueba) { std::cout << "[ERROR RUTA] No existe o no se puede abrir: " << archivo << std::endl; return; }
        carpeta = archivo.substr(0, archivo.find_last_of('/'));
        Assimp::Importer lector;
        lector.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_MATERIALS, true);
        lector.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_TEXTURES, true);
        const aiScene* escena = lector.ReadFile(archivo,
            aiProcess_Triangulate | aiProcess_FlipUVs |
            aiProcess_GenNormals | aiProcess_PreTransformVertices);

        if (!escena || !escena->mRootNode || escena->mNumMeshes == 0)
        {
            std::cout << "Modelo no disponible o sin mallas: " << archivo
                << "\n" << lector.GetErrorString() << "\n";
            return;
        }

        for (unsigned int m = 0; m < escena->mNumMeshes; ++m)
        {
            aiMesh* malla = escena->mMeshes[m];
            if (!malla->mNumVertices || !malla->mNumFaces) continue;
            std::vector<Vertex> verticesModelo;
            std::vector<GLuint> indicesModelo;

            for (unsigned int i = 0; i < malla->mNumVertices; ++i)
            {
                Vertex v{};
                v.Position = glm::vec3(malla->mVertices[i].x,
                    malla->mVertices[i].y, malla->mVertices[i].z);
                v.Normal = malla->HasNormals()
                    ? glm::vec3(malla->mNormals[i].x, malla->mNormals[i].y, malla->mNormals[i].z)
                    : glm::vec3(0, 1, 0);
                v.TexCoords = malla->mTextureCoords[0]
                    ? glm::vec2(malla->mTextureCoords[0][i].x, malla->mTextureCoords[0][i].y)
                    : glm::vec2(0);
                for (int j = 0; j < MAX_BONE_INFLUENCE; ++j)
                {
                    v.m_BoneIDs[j] = -1;
                    v.m_Weights[j] = 0.0f;
                }
                for (int j = 0; j < 3; ++j)
                {
                    if (v.Position[j] < minimo[j]) minimo[j] = v.Position[j];
                    if (v.Position[j] > maximo[j]) maximo[j] = v.Position[j];
                }
                verticesModelo.push_back(v);
            }

            for (unsigned int i = 0; i < malla->mNumFaces; ++i)
                for (unsigned int j = 0; j < malla->mFaces[i].mNumIndices; ++j)
                    indicesModelo.push_back(malla->mFaces[i].mIndices[j]);

            if (indicesModelo.empty()) continue;
            Texture textura;
            textura.id = cargarTextura(escena->mMaterials[malla->mMaterialIndex], escena);
            textura.type = "texture_diffuse";
            std::vector<Texture> material = { textura };
            piezas.emplace_back(verticesModelo, indicesModelo, material);
        }
        disponible = !piezas.empty();
    }

    void dibujar(Shader& shader, const glm::vec3& posicion,
        float altura, float giro = 0.0f, bool recortarAlpha = true)
    {
        if (!disponible) return;
        if (maximo.y - minimo.y < 0.000001f) return;
        dibujarMatriz(shader, matrizColocacion(posicion, altura, giro), recortarAlpha);
    }

    void dibujarInclinado(Shader& shader, const glm::vec3& posicion, float altura, float giro,
        float grados, bool frentePositivo, bool levantarFrente = true)
    {
        if (!disponible) return;
        if (maximo.y - minimo.y < 0.000001f) return;
        glm::mat4 base = matrizColocacion(posicion, altura, giro);
        if (std::abs(grados) > 0.0001f)
        {
            bool ejeZ = ejeLargoEsZ();
            float cx = (minimo.x + maximo.x) * 0.5f;
            float cz = (minimo.z + maximo.z) * 0.5f;
            float trasero = ejeZ ? (frentePositivo ? minimo.z : maximo.z)
                : (frentePositivo ? minimo.x : maximo.x);
            float delantero = ejeZ ? (frentePositivo ? maximo.z : minimo.z)
                : (frentePositivo ? maximo.x : minimo.x);
            float e = levantarFrente ? trasero : delantero;
            glm::vec3 pivote = ejeZ ? glm::vec3(cx, minimo.y, e) : glm::vec3(e, minimo.y, cz);
            base = base * cabeceoAlrededor(pivote, grados, frentePositivo, !levantarFrente);
        }
        dibujarMatriz(shader, base, true);
    }

    void separarCabeza(float fraccionLargo, float fraccionAltura, float alturaPivote, bool frentePositivo)
    {
        if (!disponible) return;
        glm::vec3 tam = maximo - minimo;
        bool ejeZ = ejeLargoEsZ();
        float largo = ejeZ ? tam.z : tam.x;
        float minEje = ejeZ ? minimo.z : minimo.x;
        float maxEje = ejeZ ? maximo.z : maximo.x;
        float limiteEje = frentePositivo ? maxEje - largo * fraccionLargo
            : minEje + largo * fraccionLargo;
        float limiteAlto = minimo.y + tam.y * fraccionAltura;

        std::vector<Mesh> cuerpo;
        for (auto& pieza : piezas)
        {
            std::vector<Vertex> vc, vh;
            std::vector<GLuint> ic, ih;
            for (size_t i = 0; i + 2 < pieza.indices.size(); i += 3)
            {
                glm::vec3 p0 = pieza.vertices[pieza.indices[i]].Position;
                glm::vec3 p1 = pieza.vertices[pieza.indices[i + 1]].Position;
                glm::vec3 p2 = pieza.vertices[pieza.indices[i + 2]].Position;
                glm::vec3 c = (p0 + p1 + p2) / 3.0f;
                float e = ejeZ ? c.z : c.x;
                bool delante = frentePositivo ? (e >= limiteEje) : (e <= limiteEje);
                bool esCabeza = delante && c.y >= limiteAlto;
                std::vector<Vertex>& vv = esCabeza ? vh : vc;
                std::vector<GLuint>& ii = esCabeza ? ih : ic;
                for (int k = 0; k < 3; ++k)
                {
                    ii.push_back(static_cast<GLuint>(vv.size()));
                    vv.push_back(pieza.vertices[pieza.indices[i + k]]);
                }
            }
            if (!ic.empty()) cuerpo.emplace_back(vc, ic, pieza.textures);
            if (!ih.empty()) cabezaPiezas.emplace_back(vh, ih, pieza.textures);
        }
        if (cabezaPiezas.empty()) return;
        piezas = cuerpo;
        cabezaFrentePos = frentePositivo;
        float cx = (minimo.x + maximo.x) * 0.5f;
        float cz = (minimo.z + maximo.z) * 0.5f;
        float py = minimo.y + tam.y * alturaPivote;
        pivoteCabeza = ejeZ ? glm::vec3(cx, py, limiteEje) : glm::vec3(limiteEje, py, cz);
    }

    void dibujarConCabeza(Shader& shader, const glm::vec3& posicion, float altura,
        float giro, float grados)
    {
        if (!disponible) return;
        if (maximo.y - minimo.y < 0.000001f) return;
        glm::mat4 base = matrizColocacion(posicion, altura, giro);
        dibujarLista(shader, base, true, piezas);
        if (cabezaPiezas.empty()) return;
        glm::mat4 cabeza = base * cabeceoAlrededor(pivoteCabeza, grados, cabezaFrentePos, true);
        dibujarLista(shader, cabeza, true, cabezaPiezas);
    }

    void dibujarMatriz(Shader& shader, const glm::mat4& matriz, bool recortarAlpha = true)
    {
        if (!disponible) return;
        dibujarLista(shader, matriz, recortarAlpha, piezas);
    }

    void dibujarAccesorio(Shader& shader, const glm::mat4& soporte, float ancho)
    {
        if (!disponible) return;
        glm::vec3 tam = maximo - minimo;
        float medida = tam.x > tam.z ? tam.x : tam.z;
        if (medida < 0.000001f) return;
        glm::mat4 matriz = glm::scale(soporte, glm::vec3(ancho / medida));
        matriz = glm::translate(matriz, -glm::vec3((minimo.x + maximo.x) * 0.5f,
            minimo.y, (minimo.z + maximo.z) * 0.5f));
        dibujarMatriz(shader, matriz);
    }

    void dibujarTramo(Shader& shader, glm::vec3 inicio, glm::vec3 fin, float alto)
    {
        if (!disponible) return;
        glm::vec3 tam = maximo - minimo;
        bool sobreZ = tam.z > tam.x;
        float largo = sobreZ ? tam.z : tam.x;
        if (largo < 0.000001f || tam.y < 0.000001f) return;
        inicio.y = alturaTerreno(inicio.x, inicio.z);
        fin.y = alturaTerreno(fin.x, fin.z);
        glm::vec3 d = fin - inicio;
        float distancia = std::sqrt(d.x * d.x + d.z * d.z);
        glm::mat4 matriz = glm::translate(glm::mat4(1), (inicio + fin) * 0.5f);
        matriz = glm::rotate(matriz, -std::atan2(d.z, d.x), glm::vec3(0, 1, 0));
        float escala = alto / tam.y;
        matriz = glm::scale(matriz, glm::vec3(distancia / largo, escala, escala));
        if (sobreZ) matriz = glm::rotate(matriz, glm::radians(90.0f), glm::vec3(0, 1, 0));
        matriz = glm::translate(matriz, -glm::vec3((minimo.x + maximo.x) * 0.5f,
            minimo.y, (minimo.z + maximo.z) * 0.5f));
        dibujarMatriz(shader, matriz, false);
    }

    glm::vec3 dimensiones(float altura) const
    {
        float alto = maximo.y - minimo.y;
        if (!disponible || alto < 0.000001f) return glm::vec3(0);
        return (maximo - minimo) * (altura / alto);
    }

    float frenteEnAltura(float altura, float fraccion) const
    {
        float alto = maximo.y - minimo.y;
        if (!disponible || alto < 0.000001f) return 0.0f;
        float corte = minimo.y + alto * fraccion;
        float frente = minimo.z;
        bool encontrado = false;
        for (const auto& pieza : piezas)
            for (size_t i = 0; i + 2 < pieza.indices.size(); i += 3)
                for (int e = 0; e < 3; ++e)
                {
                    glm::vec3 a = pieza.vertices[pieza.indices[i + e]].Position;
                    glm::vec3 b = pieza.vertices[pieza.indices[i + (e + 1) % 3]].Position;
                    if ((a.y <= corte && b.y >= corte) || (b.y <= corte && a.y >= corte))
                    {
                        float z;
                        if (std::abs(b.y - a.y) < 0.000001f) z = std::max(a.z, b.z);
                        else z = a.z + (b.z - a.z) * (corte - a.y) / (b.y - a.y);
                        frente = encontrado ? std::max(frente, z) : z;
                        encontrado = true;
                    }
                }
        if (!encontrado) frente = maximo.z;
        return (frente - (minimo.z + maximo.z) * 0.5f) * altura / alto;
    }

    float medioEspesorAspas(float diametro) const
    {
        glm::vec3 tam = maximo - minimo;
        float mayor = std::max(tam.x, std::max(tam.y, tam.z));
        float menor = std::min(tam.x, std::min(tam.y, tam.z));
        return disponible && mayor > 0.000001f ? menor * diametro / mayor * 0.5f : 0.0f;
    }

    void dibujarAspas(Shader& shader, const glm::mat4& soporte,
        float diametro, float angulo)
    {
        if (!disponible) return;
        glm::vec3 tam = maximo - minimo;
        int fino = 2;
        if (tam.x < tam[fino]) fino = 0;
        if (tam.y < tam[fino]) fino = 1;
        float mayor = tam.x;
        if (tam.y > mayor) mayor = tam.y;
        if (tam.z > mayor) mayor = tam.z;
        if (mayor < 0.000001f) return;
        glm::mat4 matriz = glm::rotate(soporte, glm::radians(angulo), glm::vec3(0, 0, 1));
        if (fino == 0) matriz = glm::rotate(matriz, glm::radians(90.0f), glm::vec3(0, 1, 0));
        if (fino == 1) matriz = glm::rotate(matriz, glm::radians(90.0f), glm::vec3(1, 0, 0));
        matriz = glm::scale(matriz, glm::vec3(diametro / mayor));
        matriz = glm::translate(matriz, -(minimo + maximo) * 0.5f);
        dibujarMatriz(shader, matriz);
    }

};

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode);
void MouseCallback(GLFWwindow* window, double xPos, double yPos);
void DoMovement();
void AnimationKeys();
void AnimacionCreeper(float dt);
void AnimacionesExtra(float dt);

const GLuint WIDTH = 1920, HEIGHT = 1000;
int SCREEN_WIDTH, SCREEN_HEIGHT;

Camera  camera(glm::vec3(1.5f, 0.5f, 3.0f), glm::vec3(0.0f, 1.0f, 0.0f), 270.0f);
double lastX = WIDTH / 2.0;
double lastY = HEIGHT / 2.0;
bool keys[1024];
bool firstMouse = true;

glm::vec3 lightPos(0.0f, 0.0f, 0.0f);
bool active;

// Luz direccional blanca y naranja.
bool luzNaranja = false;

bool luzRojaActiva = true;
bool luzAzulActiva = true;

float alturaCasa = 2.2f;
float alturaCabra = 0.45f;
float alturaCaballo = 1.1f;
float alturaBarril = 0.45f;
float alturaBandera = 2.8f;
float alturaCactus = 0.85f;
float alturaAspas = 4.2f;
float alturaMolino = 6.0f;
float alturaPozo = 1.0f;
float alturaIglesia = 3.4f;
float alturaOveja = 0.48f;
float alturaFogata = 0.5f;
float alturaPersona = 0.8f;
float alturaValla = 0.55f;
float anchoSombrero = 0.42f;
float ajusteSombrero = -0.02f;

// Aspas del molino en movimiento.
glm::vec3 posicionMolino(-9.0f, 0.0f, -5.0f);
float giroMolino = 0.0f;
float alturaEjeMolino = 0.72f;
float separacionAspas = 0.06f;
float velocidadAspas = 45.0f;
float anguloAspas = 0.0f;

// Integración y movimiento del zorro.
float escalaZorro = 0.1f;
float zorroPosX = 5.0f;
float zorroPosY = 0.12f;
float zorroPosZ = 0.0f;

bool zorroCaminando = false;
float zorroInicioZ = zorroPosZ;
float zorroRecorrido = 0.0f;
float zorroDistancia = 2.0f;
float zorroVelocidad = 1.0f;
float zorroTiempoPasos = 0.0f;
float zorroGiro = 0.0f;
float zorroAnguloPatas = 0.0f;
float zorroAnguloCola = 0.0f;
float zorroAnguloCabeza = 0.0f;

bool zorroSaltando = false;
float zorroTiempoSalto = 0.0f;
float zorroDuracionSalto = 0.8f;
float zorroAlturaSalto = 0.35f;
float zorroElevacion = 0.0f;

// Movimiento y explosión del Creeper.
bool creeperVisible = true;
bool explota = false;
bool pausa = false;
bool creeperCaminando = true;
float posicionInicialX = 1.5f;
float posicionInicialY = 0.05f;
float posicionInicialZ = -10.0f;
float posicionActualX = 1.5f;
float posicionActualZ = -10.0f;
float limiteInicial = -10.0f;
float limiteFinal = -1.0f;
float direccionMovimiento = 1.0f;
float velocidadMovimiento = 0.65f;
float escalaActual = 0.3f;
float escalaInicial = 0.3f;
float escalaMaxima = 0.70f;
float velocidadExplosion = 0.55f;
float rotacionCreeper = 0.0f;
float movimientoVertical = 0.0f;
float balanceo = 0.0f;
float tiempoDesaparicion = 0.0f;
float duracionDesaparicion = 2.0f;

// Animaciones del caballo, hacha y oveja.
bool caballoRelinchando = false;
float caballoTiempo = 0.0f;
float caballoDuracion = 1.6f;
float anguloRelincho = 35.0f;
float anguloCaballo = 0.0f;
bool caballoFrenteMas = true;
bool caballoLevantaFrente = true;

bool hachaGolpeando = false;
float hachaTiempo = 0.0f;
float hachaDuracion = 2.4f;
int hachaGolpes = 3;
float hachaAngulo = 0.0f;

bool ovejaComiendo = false;
float ovejaTiempo = 0.0f;
float ovejaDuracion = 3.0f;
float ovejaAnguloMax = 65.0f;
float ovejaAngulo = 0.0f;
bool ovejaFrenteMas = true;
float ovejaFraccionLargo = 0.30f;
float ovejaFraccionAltura = 0.40f;
float ovejaAlturaCuello = 0.75f;

// Distribución de árboles.
glm::vec3 posicionesArboles[] = {
    {-14.00f, 0.06985f, -8.00f},
    {-7.00f, 0.06094f, -13.00f},
    {-1.00f, -0.00042f, -15.00f},
    {10.00f, -0.00041f, -12.00f},
    {-15.00f, 2.00230f, 0.00f},
    {-12.00f, 3.58176f, 7.00f},
    {-5.00f, 2.37925f, 10.00f},
    {1.00f, 1.00695f, 11.00f},
    {11.00f, 0.00537f, 9.00f}
};

glm::vec3 posicionesCactus[] = {
    {9.00f, -0.00000f, -10.00f},
    {12.00f, -0.00000f, -8.00f},
    {14.00f, -0.00000f, -4.00f},
    {11.00f, -0.00000f, -1.00f},
    {14.00f, 0.00000f, 2.00f},
    {11.00f, 0.00000f, 5.00f},
    {8.00f, 0.00289f, 7.00f},
    {16.00f, 0.00000f, 6.00f},
    {17.00f, -0.00000f, -9.00f},
    {17.00f, 0.00000f, 0.00f}
};

glm::vec3 pointLightPositions[] = {
    glm::vec3(0.0f,2.0f, 0.0f)
};

#define MAX_FRAMES 20
typedef struct _frame {
    float rotGato;
    float rotGatoInc;
    float gatoPosX;
    float gatoPosY;
    float gatoPosZ;
    float incX;
    float incY;
    float incZ;
    float headGato;
    float headIncGato;
    float FLegsGato;
    float FLegsIncGato;
    float BLegsGato;
    float BLegsIncGato;
    float tailGato;
    float tailIncGato;
    float inclinacionGato;
    float inclinacionIncGato;
} FRAME;

FRAME KeyFrame[MAX_FRAMES];
int FrameIndex = 0;
bool play = false;
int playIndex = 0;

float vertices[] = {
     -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
        0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
       -0.5f,  0.5f, -0.5f,  0.0f,  0.0f, -1.0f,
       -0.5f, -0.5f, -0.5f,  0.0f,  0.0f, -1.0f,

       -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
        0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
       -0.5f,  0.5f,  0.5f,  0.0f,  0.0f,  1.0f,
       -0.5f, -0.5f,  0.5f,  0.0f,  0.0f,  1.0f,

       -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
       -0.5f,  0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
       -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
       -0.5f, -0.5f, -0.5f, -1.0f,  0.0f,  0.0f,
       -0.5f, -0.5f,  0.5f, -1.0f,  0.0f,  0.0f,
       -0.5f,  0.5f,  0.5f, -1.0f,  0.0f,  0.0f,

        0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
        0.5f,  0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
        0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
        0.5f, -0.5f, -0.5f,  1.0f,  0.0f,  0.0f,
        0.5f, -0.5f,  0.5f,  1.0f,  0.0f,  0.0f,
        0.5f,  0.5f,  0.5f,  1.0f,  0.0f,  0.0f,

       -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
        0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,
        0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
        0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
       -0.5f, -0.5f,  0.5f,  0.0f, -1.0f,  0.0f,
       -0.5f, -0.5f, -0.5f,  0.0f, -1.0f,  0.0f,

       -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
        0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f,
        0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
        0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
       -0.5f,  0.5f,  0.5f,  0.0f,  1.0f,  0.0f,
       -0.5f,  0.5f, -0.5f,  0.0f,  1.0f,  0.0f
};

class HachaPersonaje
{
    GLuint vao = 0, vbo = 0;
    GLuint colores[3] = {};
public:
    explicit HachaPersonaje(const float* cubo)
    {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, 36 * 6 * sizeof(float), cubo, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        const unsigned char tonos[3][4] = { {112, 66, 30, 255}, {125, 133, 142, 255}, {205, 211, 217, 255} };
        glGenTextures(3, colores);
        for (int i = 0; i < 3; ++i)
        {
            glBindTexture(GL_TEXTURE_2D, colores[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, tonos[i]);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        glBindVertexArray(0);
    }

    void dibujar(Shader& shader, const glm::mat4& mano, float largo)
    {
        glUniform1i(glGetUniformLocation(shader.Program, "useBones"), 0);
        glUniform1i(glGetUniformLocation(shader.Program, "transparency"), 0);
        glUniform1i(glGetUniformLocation(shader.Program, "material.diffuse"), 0);
        glUniform1i(glGetUniformLocation(shader.Program, "material.specular"), 0);
        glActiveTexture(GL_TEXTURE0);
        glBindVertexArray(vao);
        glm::mat4 base = glm::scale(mano, glm::vec3(largo));
        auto pieza = [&](glm::vec3 centro, glm::vec3 medida, int color) {
            glm::mat4 m = glm::scale(glm::translate(base, centro), medida);
            glUniformMatrix4fv(glGetUniformLocation(shader.Program, "model"), 1, GL_FALSE, glm::value_ptr(m));
            glBindTexture(GL_TEXTURE_2D, colores[color]);
            glDrawArrays(GL_TRIANGLES, 0, 36);
            };
        pieza({ 0, 0.25f, 0 }, { 0.085f, 1.0f, 0.085f }, 0);
        pieza({ 0.035f, 0.65f, 0 }, { 0.22f, 0.18f, 0.14f }, 1);
        pieza({ 0.20f, 0.65f, 0 }, { 0.22f, 0.30f, 0.10f }, 1);
        pieza({ 0.325f, 0.65f, 0 }, { 0.065f, 0.34f, 0.045f }, 2);
        glBindVertexArray(0);
    }

    void liberar()
    {
        glDeleteTextures(3, colores);
        glDeleteBuffers(1, &vbo);
        glDeleteVertexArrays(1, &vao);
    }
};

float sunAngle = 0.0f;
float sunSpeed = 0.2f;
float sunRadius = 10.0f;
bool dayNightCycle = true;
glm::vec3 sunPosition(0.0f);

glm::vec3 Light1 = glm::vec3(0);

bool playFBX = false;
Animation* g_SteveAnim = nullptr;
Animator* g_SteveAnimator = nullptr;
float steveSpeed = 1.0f;
int steveEstado = 0;
float steveDir = 1.0f;
float stevePosX = 0.0f, stevePosY = 0.0f, stevePosZ = 0.0f;
int i_max_steps = 140;
int i_curr_steps = 0;

GLfloat deltaTime = 0.0f;
double lastFrame = 0.0;

int main()
{

    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    glfwInit();
    GLFWwindow* window = glfwCreateWindow(WIDTH, HEIGHT, "Extraordinario CG", nullptr, nullptr);

    if (nullptr == window)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();

        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);

    glfwGetFramebufferSize(window, &SCREEN_WIDTH, &SCREEN_HEIGHT);

    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCursorPosCallback(window, MouseCallback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    glewExperimental = GL_TRUE;

    if (GLEW_OK != glewInit())
    {
        std::cout << "Failed to initialize GLEW" << std::endl;
        return EXIT_FAILURE;
    }

    glViewport(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);

    Shader lightingShader("Shader/lighting.vs", "Shader/lighting.frag");
    Shader lampShader("Shader/lamp.vs", "Shader/lamp.frag");
    Shader skyboxShader("Shader/SkyBox.vs", "Shader/SkyBox.frag");

    cargarAlturasTerreno();
    // Piso y texturas.
    Model Aldea((char*)"ModelsExtra/Piso/Piso.fbx");

    Model tree((char*)"ModelsExtra/Tree/Tree.obj");

    Model zorroCuerpo((char*)"ModelsExtra/Zorro/cuerpo.obj");
    Model zorroCabeza((char*)"ModelsExtra/Zorro/cabeza.obj");
    Model zorroCola((char*)"ModelsExtra/Zorro/cola.obj");
    Model zorroPataID((char*)"ModelsExtra/Zorro/pataIzqDelante.obj");
    Model zorroPataDD((char*)"ModelsExtra/Zorro/pataDerDelante.obj");
    Model zorroPataIT((char*)"ModelsExtra/Zorro/pataIzqDetras.obj");
    Model zorroPataDT((char*)"ModelsExtra/Zorro/pataDerDetras.obj");

    Model creeper((char*)"ModelsExtra/Creeper/Creeper.fbx");

    ObjetoEscenario casa("ModelsExtra/Casa/Casa.fbx");
    ObjetoEscenario cabra("ModelsExtra/GOAT/Goat.fbx");
    ObjetoEscenario caballo("ModelsExtra/Caballo/Caballo.fbx");
    ObjetoEscenario barril("ModelsExtra/Barril/barril.fbx");
    ObjetoEscenario bandera("ModelsExtra/Bandera/bandera.fbx");
    ObjetoEscenario cactus("ModelsExtra/Cactus/cactus.fbx");
    ObjetoEscenario aspas("ModelsExtra/Aspas/Aspas.fbx");
    ObjetoEscenario molino("ModelsExtra/Molino/Molino.fbx");
    ObjetoEscenario pozo("ModelsExtra/Pozo/Pozo.fbx");
    ObjetoEscenario iglesia("ModelsExtra/Iglesia/Iglesia.fbx");
    ObjetoEscenario oveja("ModelsExtra/Oveja/Oveja.fbx");
    ObjetoEscenario fogata("ModelsExtra/Fogata/Fogata.fbx");
    ObjetoEscenario sombrero("ModelsExtra/Sombrero/Sombrero.fbx");
    ObjetoEscenario valla("ModelsExtra/Cerca/Cerca.fbx");
    ObjetoEscenario persona("ModelsExtra/Persona/Persona.fbx");
    HachaPersonaje hacha(vertices);

    oveja.separarCabeza(ovejaFraccionLargo, ovejaFraccionAltura, ovejaAlturaCuello, ovejaFrenteMas);

    Model Steve((char*)"ModelsExtra/Steve/steveAnimated.fbx");
    g_SteveAnim = new Animation("ModelsExtra/Steve/steveAnimated.fbx", &Steve);
    g_SteveAnimator = new Animator(g_SteveAnim);
    g_SteveAnimator->Reset();

    GLfloat skyboxVertices[] = {

        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
        1.0f, -1.0f, -1.0f,
        1.0f, -1.0f, -1.0f,
        1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        1.0f, -1.0f, -1.0f,
        1.0f, -1.0f,  1.0f,
        1.0f,  1.0f,  1.0f,
        1.0f,  1.0f,  1.0f,
        1.0f,  1.0f, -1.0f,
        1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        1.0f,  1.0f,  1.0f,
        1.0f,  1.0f,  1.0f,
        1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
        1.0f,  1.0f, -1.0f,
        1.0f,  1.0f,  1.0f,
        1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
        1.0f, -1.0f, -1.0f,
        1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
        1.0f, -1.0f,  1.0f
    };

    GLuint indices[] =
    {
        0,1,2,3,
        4,5,6,7,
        8,9,10,11,
        12,13,14,15,
        16,17,18,19,
        20,21,22,23,
        24,25,26,27,
        28,29,30,31,
        32,33,34,35
    };

    GLuint VBO, VAO, EBO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);

    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(GLfloat), (GLvoid*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);

    lightingShader.Use();
    glUniform1i(glGetUniformLocation(lightingShader.Program, "material.diffuse"), 0);
    glUniform1i(glGetUniformLocation(lightingShader.Program, "material.specular"), 0);

    GLuint skyboxVBO, skyboxVAO;
    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);
    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (GLvoid*)0);

    vector < const GLchar*> faces;
    faces.push_back("SkyBox/right.jpg");
    faces.push_back("SkyBox/left.jpg");
    faces.push_back("SkyBox/top.jpg");
    faces.push_back("SkyBox/bottom.jpg");
    faces.push_back("SkyBox/back.jpg");
    faces.push_back("SkyBox/front.jpg");

    GLuint cubemapTexture = TextureLoading::LoadCubemap(faces);

    glm::mat4 projection = glm::perspective(camera.GetZoom(), (GLfloat)SCREEN_WIDTH / (GLfloat)SCREEN_HEIGHT, 0.1f, 100.0f);

    while (!glfwWindowShouldClose(window))
    {

        double currentFrame = glfwGetTime();
        deltaTime = static_cast<GLfloat>(currentFrame - lastFrame);
        lastFrame = currentFrame;
        anguloAspas = std::fmod(anguloAspas + velocidadAspas * deltaTime, 360.0f);

        sunPosition = glm::vec3(0.0f, 10.0f, 0.0f);
        float dayFactor = 0.5f;

        glm::vec3 sunDir = glm::normalize(glm::vec3(-0.5f, -1.0f, -0.5f));

        glfwPollEvents();
        DoMovement();
        AnimationKeys();
        zorroPosY = alturaTerreno(zorroPosX, zorroPosZ) + 0.12f;

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        glEnable(GL_DEPTH_TEST);

        glm::mat4 modelTemp = glm::mat4(1.0f);

        lightingShader.Use();

        glUniform1i(glGetUniformLocation(lightingShader.Program, "diffuse"), 0);

        GLint viewPosLoc = glGetUniformLocation(lightingShader.Program, "viewPos");
        glUniform3f(viewPosLoc, camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);

        glm::vec3 colorLuz(1.0f, 1.0f, 1.0f);
        if (luzNaranja) colorLuz = glm::vec3(1.0f, 0.5f, 0.0f);
        float intensidadLuz = 0.5f;

        glm::vec3 ambient = colorLuz * intensidadLuz * 0.4f;
        glm::vec3 diffuse = colorLuz * intensidadLuz;
        glm::vec3 specular = colorLuz * intensidadLuz * 0.3f;

        glUniform3f(glGetUniformLocation(lightingShader.Program, "dirLight.direction"), sunDir.x, sunDir.y, sunDir.z);
        glUniform3f(glGetUniformLocation(lightingShader.Program, "dirLight.ambient"), ambient.x, ambient.y, ambient.z);
        glUniform3f(glGetUniformLocation(lightingShader.Program, "dirLight.diffuse"), diffuse.x, diffuse.y, diffuse.z);
        glUniform3f(glGetUniformLocation(lightingShader.Program, "dirLight.specular"), specular.x, specular.y, specular.z);

        // Luces de colores sobre Steve.
        glm::vec3 objetivoSteve(2.0f + stevePosX,
            alturaTerreno(2.0f, stevePosZ) + 0.8f + stevePosY,
            stevePosZ);
        glm::vec3 posicionLuzRoja = objetivoSteve + glm::vec3(-1.4f, 1.3f, 0.8f);
        glm::vec3 posicionLuzAzul = objetivoSteve + glm::vec3(1.4f, 1.3f, 0.8f);
        glm::vec3 direccionLuzRoja = glm::normalize(objetivoSteve - posicionLuzRoja);
        glm::vec3 direccionLuzAzul = glm::normalize(objetivoSteve - posicionLuzAzul);

        GLuint prog = lightingShader.Program;
        glUniform3f(glGetUniformLocation(prog, "spotLights[0].position"), posicionLuzRoja.x, posicionLuzRoja.y, posicionLuzRoja.z);
        glUniform3f(glGetUniformLocation(prog, "spotLights[0].direction"), direccionLuzRoja.x, direccionLuzRoja.y, direccionLuzRoja.z);
        glUniform3f(glGetUniformLocation(prog, "spotLights[0].ambient"), 0.15f, 0.0f, 0.0f);
        glUniform3f(glGetUniformLocation(prog, "spotLights[0].diffuse"), 1.0f, 0.0f, 0.0f);
        glUniform3f(glGetUniformLocation(prog, "spotLights[0].specular"), 1.0f, 0.2f, 0.2f);
        glUniform1f(glGetUniformLocation(prog, "spotLights[0].cutOff"), glm::cos(glm::radians(12.5f)));
        glUniform1f(glGetUniformLocation(prog, "spotLights[0].outerCutOff"), glm::cos(glm::radians(20.0f)));
        glUniform1f(glGetUniformLocation(prog, "spotLights[0].constant"), 1.0f);
        glUniform1f(glGetUniformLocation(prog, "spotLights[0].linear"), 0.09f);
        glUniform1f(glGetUniformLocation(prog, "spotLights[0].quadratic"), 0.032f);
        glUniform1i(glGetUniformLocation(prog, "spotLights[0].activo"), luzRojaActiva ? 1 : 0);
        glUniform3f(glGetUniformLocation(prog, "spotLights[1].position"), posicionLuzAzul.x, posicionLuzAzul.y, posicionLuzAzul.z);
        glUniform3f(glGetUniformLocation(prog, "spotLights[1].direction"), direccionLuzAzul.x, direccionLuzAzul.y, direccionLuzAzul.z);
        glUniform3f(glGetUniformLocation(prog, "spotLights[1].ambient"), 0.0f, 0.0f, 0.15f);
        glUniform3f(glGetUniformLocation(prog, "spotLights[1].diffuse"), 0.0f, 0.0f, 1.0f);
        glUniform3f(glGetUniformLocation(prog, "spotLights[1].specular"), 0.2f, 0.2f, 1.0f);
        glUniform1f(glGetUniformLocation(prog, "spotLights[1].cutOff"), glm::cos(glm::radians(12.5f)));
        glUniform1f(glGetUniformLocation(prog, "spotLights[1].outerCutOff"), glm::cos(glm::radians(20.0f)));
        glUniform1f(glGetUniformLocation(prog, "spotLights[1].constant"), 1.0f);
        glUniform1f(glGetUniformLocation(prog, "spotLights[1].linear"), 0.09f);
        glUniform1f(glGetUniformLocation(prog, "spotLights[1].quadratic"), 0.032f);
        glUniform1i(glGetUniformLocation(prog, "spotLights[1].activo"), luzAzulActiva ? 1 : 0);

        glUniform1f(glGetUniformLocation(lightingShader.Program, "material.shininess"), 5.0f);

        glm::mat4 view;
        view = camera.GetViewMatrix();

        GLint modelLoc = glGetUniformLocation(lightingShader.Program, "model");
        GLint viewLoc = glGetUniformLocation(lightingShader.Program, "view");
        GLint projLoc = glGetUniformLocation(lightingShader.Program, "projection");

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));

        glm::mat4 model(1);
        glUniform1i(glGetUniformLocation(lightingShader.Program, "useBones"), 0);

        view = camera.GetViewMatrix();
        model = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0));
        model = glm::scale(model, glm::vec3(escalaPiso));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        Aldea.Draw(lightingShader);

        for (const auto& posicion : posicionesArboles)
        {
            model = glm::translate(glm::mat4(1.0f), posicion);
            model = glm::scale(model, glm::vec3(2.0f));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
            tree.Draw(lightingShader);
        }

        casa.dibujar(lightingShader, { -3.000f, -0.00000f, -7.000f }, alturaCasa * 1.0f, 0.0f);
        casa.dibujar(lightingShader, { -6.000f, 0.30745f, 2.000f }, alturaCasa * 0.9f, 90.0f);
        casa.dibujar(lightingShader, { 5.000f, -0.00000f, -13.000f }, alturaCasa * 0.95f, 0.0f);
        iglesia.dibujar(lightingShader, { 6.000f, -0.00000f, -7.000f }, alturaIglesia, 0.0f);
        pozo.dibujar(lightingShader, { 0.000f, -0.00000f, -3.000f }, alturaPozo, 0.0f);
        bandera.dibujar(lightingShader, { 4.000f, -0.00000f, -2.000f }, alturaBandera, 0.0f);
        barril.dibujar(lightingShader, { -1.000f, -0.00000f, -6.000f }, alturaBarril, 0.0f, false);
        barril.dibujar(lightingShader, { -1.000f, -0.00000f, -5.300f }, alturaBarril, 25.0f, false);
        caballo.dibujarInclinado(lightingShader, { 8.000f, -0.00000f, -0.500f }, alturaCaballo, -70.0f,
            anguloCaballo, caballoFrenteMas, caballoLevantaFrente);
        cabra.dibujar(lightingShader, { 7.500f, 0.00000f, 2.200f }, alturaCabra, -70.0f);
        oveja.dibujarConCabeza(lightingShader, { 8.600f, 0.00000f, 3.500f }, alturaOveja, -30.0f, ovejaAngulo);
        fogata.dibujar(lightingShader, { -2.000f, 0.31166f, 5.000f }, alturaFogata, 0.0f);
        auto dibujarPersona = [&](glm::vec3 posicion, float giro) {
            persona.dibujar(lightingShader, posicion, alturaPersona, giro);
            if (!persona.disponible) return;
            posicion.y = std::max(posicion.y, alturaTerreno(posicion.x, posicion.z));
            glm::vec3 tam = persona.dimensiones(alturaPersona);
            glm::mat4 mano = glm::translate(glm::mat4(1), posicion);
            mano = glm::rotate(mano, glm::radians(giro), glm::vec3(0, 1, 0));
            mano = glm::translate(mano, glm::vec3(tam.x * 0.42f, alturaPersona * 0.40f, 0));
            mano = glm::rotate(mano, glm::radians(hachaAngulo), glm::vec3(0, 0, 1));
            hacha.dibujar(lightingShader, mano, alturaPersona * 0.55f);
            };
        dibujarPersona({ -3.300f, 0.51014f, 4.800f }, 90.0f);
        dibujarPersona({ -0.700f, 0.11271f, 4.800f }, -90.0f);
        dibujarPersona({ 3.000f, 0.0f, -5.000f }, 30.0f);

        // Zonas cercadas.
        valla.dibujarTramo(lightingShader, { 6.000f, -0.00000f, -2.000f }, { 7.000f, -0.00000f, -2.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 7.000f, -0.00000f, -2.000f }, { 8.000f, -0.00000f, -2.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 8.000f, -0.00000f, -2.000f }, { 9.000f, -0.00000f, -2.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 9.000f, -0.00000f, -2.000f }, { 10.000f, -0.00000f, -2.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 10.000f, -0.00000f, -2.000f }, { 10.000f, -0.00000f, -1.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 10.000f, -0.00000f, -1.000f }, { 10.000f, 0.00000f, 0.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 10.000f, 0.00000f, 0.000f }, { 10.000f, 0.00000f, 1.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 10.000f, 0.00000f, 1.000f }, { 10.000f, 0.00000f, 2.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 10.000f, 0.00000f, 2.000f }, { 10.000f, 0.00000f, 3.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 10.000f, 0.00000f, 3.000f }, { 10.000f, 0.00000f, 4.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 10.000f, 0.00000f, 4.000f }, { 10.000f, 0.00000f, 5.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 10.000f, 0.00000f, 5.000f }, { 9.000f, 0.00000f, 5.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 9.000f, 0.00000f, 5.000f }, { 8.000f, 0.00000f, 5.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 8.000f, 0.00000f, 5.000f }, { 7.000f, 0.00000f, 5.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 7.000f, 0.00000f, 5.000f }, { 6.000f, 0.00030f, 5.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 6.000f, -0.00000f, -2.000f }, { 6.000f, -0.00000f, -1.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 6.000f, -0.00000f, -1.000f }, { 6.000f, 0.00000f, 0.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 6.000f, 0.00030f, 1.500f }, { 6.000f, 0.00030f, 2.375f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 6.000f, 0.00030f, 2.375f }, { 6.000f, 0.00030f, 3.250f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 6.000f, 0.00030f, 3.250f }, { 6.000f, 0.00030f, 4.125f }, alturaValla);
        valla.dibujarTramo(lightingShader, { 6.000f, 0.00030f, 4.125f }, { 6.000f, 0.00030f, 5.000f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -8.000f, 0.18391f, -0.500f }, { -8.000f, 0.45010f, 0.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -8.000f, 0.45010f, 0.500f }, { -8.000f, 0.97340f, 1.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -8.000f, 0.97340f, 1.500f }, { -8.000f, 1.22636f, 2.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -8.000f, 1.22636f, 2.500f }, { -8.000f, 1.38309f, 3.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -8.000f, 1.38309f, 3.500f }, { -8.000f, 1.53982f, 4.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -8.000f, 1.53982f, 4.500f }, { -7.000f, 1.06295f, 4.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -7.000f, 1.06295f, 4.500f }, { -6.000f, 0.69177f, 4.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -6.000f, 0.69177f, 4.500f }, { -5.000f, 0.69177f, 4.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -8.000f, 0.18391f, -0.500f }, { -7.000f, 0.07882f, -0.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -7.000f, 0.07882f, -0.500f }, { -6.000f, -0.00000f, -0.500f }, alturaValla);
        valla.dibujarTramo(lightingShader, { -6.000f, -0.00000f, -0.500f }, { -5.000f, -0.00000f, -0.500f }, alturaValla);

        molino.dibujar(lightingShader, posicionMolino, alturaMolino, giroMolino);
        if (molino.disponible && aspas.disponible)
        {
            glm::vec3 baseMolino = posicionMolino;
            baseMolino.y = std::max(baseMolino.y, alturaTerreno(baseMolino.x, baseMolino.z));
            static const float apoyoAspas = molino.frenteEnAltura(alturaMolino, alturaEjeMolino)
                + aspas.medioEspesorAspas(alturaAspas) + separacionAspas;
            glm::mat4 soporte = glm::translate(glm::mat4(1.0f), baseMolino);
            soporte = glm::rotate(soporte, glm::radians(giroMolino), glm::vec3(0, 1, 0));
            soporte = glm::translate(soporte, glm::vec3(0,
                alturaMolino * alturaEjeMolino, apoyoAspas));
            aspas.dibujarAspas(lightingShader, soporte, alturaAspas, anguloAspas);
        }

        for (int i = 0; i < static_cast<int>(sizeof(posicionesCactus) / sizeof(posicionesCactus[0])); ++i)
        {
            float factor = 0.8f + 0.15f * (i % 3);
            cactus.dibujar(lightingShader, posicionesCactus[i], alturaCactus * factor, i * 37.0f);
        }

        glUniform1i(glGetUniformLocation(lightingShader.Program, "useBones"), 0);
        glm::mat4 modeloZorro(1.0f);
        modeloZorro = glm::translate(modeloZorro,
            glm::vec3(zorroPosX, zorroPosY + zorroElevacion, zorroPosZ));
        modeloZorro = glm::rotate(modeloZorro, glm::radians(zorroGiro), glm::vec3(0, 1, 0));
        modeloZorro = glm::scale(modeloZorro, glm::vec3(escalaZorro));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modeloZorro));
        zorroCuerpo.Draw(lightingShader);

        // Movimiento de las partes del zorro.
        auto dibujarParteZorro = [&](Model& parte, const glm::vec3& punto,
            float angulo, const glm::vec3& eje)
            {
                glm::mat4 parteModelo = glm::translate(modeloZorro, punto);
                parteModelo = glm::rotate(parteModelo, glm::radians(angulo), eje);
                parteModelo = glm::translate(parteModelo, -punto);
                glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(parteModelo));
                parte.Draw(lightingShader);
            };
        dibujarParteZorro(zorroCabeza, { 0, -.1f, -.65f }, zorroAnguloCabeza, { 0, 1, 0 });
        dibujarParteZorro(zorroCola, { 0, 0, -1.8f }, zorroAnguloCola, { 0, 1, 0 });
        dibujarParteZorro(zorroPataID, { .267f, -.493f, -.918f }, zorroAnguloPatas, { 1, 0, 0 });
        dibujarParteZorro(zorroPataDD, { -.254f, -.493f, -.918f }, -zorroAnguloPatas, { 1, 0, 0 });
        dibujarParteZorro(zorroPataIT, { .267f, -.493f, -1.518f }, -zorroAnguloPatas, { 1, 0, 0 });
        dibujarParteZorro(zorroPataDT, { -.275f, -.493f, -1.518f }, zorroAnguloPatas, { 1, 0, 0 });

        if (creeperVisible)
        {
            float yCreeper = alturaTerreno(posicionActualX, posicionActualZ) + posicionInicialY;
            glm::mat4 modeloCreeper(1.0f);
            modeloCreeper = glm::translate(modeloCreeper,
                glm::vec3(posicionActualX, yCreeper + movimientoVertical, posicionActualZ));
            modeloCreeper = glm::rotate(modeloCreeper, glm::radians(rotacionCreeper), glm::vec3(0.0f, 1.0f, 0.0f));
            modeloCreeper = glm::rotate(modeloCreeper, glm::radians(balanceo), glm::vec3(0.0f, 0.0f, 1.0f));
            modeloCreeper = glm::rotate(modeloCreeper, glm::radians(-90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
            modeloCreeper = glm::scale(modeloCreeper, glm::vec3(escalaActual));
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modeloCreeper));
            creeper.Draw(lightingShader);
        }

        // Animación de Steve y hacha.
        bool dibujarAnimado = playFBX;
        if (playFBX)
        {
            g_SteveAnimator->UpdateAnimation(deltaTime);
            stevePosZ += steveDir * steveSpeed * deltaTime;
            if (steveEstado == 0 && stevePosZ >= 2.8f)
            {
                stevePosZ = 2.8f;
                steveDir = -1.0f;
                steveEstado = 1;
            }
            else if (steveEstado == 1 && stevePosZ <= 0.0f)
            {
                stevePosZ = 0.0f;
                steveEstado = 2;
                playFBX = false;
            }
        }
        const auto& matricesSteve = g_SteveAnimator->GetFinalBoneMatrices();
        if (dibujarAnimado)
        {
            for (int i = 0; i < static_cast<int>(matricesSteve.size()); ++i)
            {
                std::string nombre = "finalBonesMatrices[" + std::to_string(i) + "]";
                glUniformMatrix4fv(glGetUniformLocation(lightingShader.Program, nombre.c_str()),
                    1, GL_FALSE, glm::value_ptr(matricesSteve[i]));
            }
        }
        glm::vec3 posicionesSteve[] = {
            {2.0f, 0.1f, 0.0f}
        };
        for (const auto& origenSteve : posicionesSteve)
        {
            glm::vec3 posicionActual = origenSteve + glm::vec3(stevePosX, stevePosY, stevePosZ);
            posicionActual.y = alturaTerreno(posicionActual.x, posicionActual.z) + 0.1f + stevePosY;
            glm::mat4 modeloSteve = glm::translate(glm::mat4(1.0f), posicionActual);
            if (dibujarAnimado)
            {
                if (steveDir < 0)
                    modeloSteve = glm::rotate(modeloSteve, glm::radians(180.0f), glm::vec3(0, 1, 0));
            }
            else modeloSteve = glm::rotate(modeloSteve, glm::radians(-90.0f), glm::vec3(1, 0, 0));
            modeloSteve = glm::scale(modeloSteve, glm::vec3(0.1f));
            glUniform1i(glGetUniformLocation(lightingShader.Program, "useBones"), dibujarAnimado ? 1 : 0);
            glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(modeloSteve));
            Steve.Draw(lightingShader);

            glm::mat4 manoSteve = modeloSteve;
            auto brazo = Steve.GetBoneInfoMap().find("Braco.R");
            if (dibujarAnimado && brazo != Steve.GetBoneInfoMap().end() &&
                brazo->second.id >= 0 && brazo->second.id < static_cast<int>(matricesSteve.size()))
                manoSteve = manoSteve * matricesSteve[brazo->second.id];
            manoSteve = glm::translate(manoSteve, glm::vec3(0.0063f, -1.5040f, 3.20f));
            manoSteve = glm::rotate(manoSteve, glm::radians(90.0f), glm::vec3(1, 0, 0));
            hacha.dibujar(lightingShader, manoSteve, 4.4f);

            glm::mat4 soporteSombrero = modeloSteve;
            auto cabeza = Steve.GetBoneInfoMap().find("Cabeca");
            if (dibujarAnimado && cabeza != Steve.GetBoneInfoMap().end() &&
                cabeza->second.id >= 0 && cabeza->second.id < static_cast<int>(matricesSteve.size()))
                soporteSombrero = soporteSombrero * matricesSteve[cabeza->second.id];
            soporteSombrero = glm::translate(soporteSombrero,
                glm::vec3(0.0f, 0.0f, 8.0073f + ajusteSombrero / 0.1f));
            soporteSombrero = glm::rotate(soporteSombrero, glm::radians(90.0f), glm::vec3(1, 0, 0));
            sombrero.dibujarAccesorio(lightingShader, soporteSombrero, anchoSombrero / 0.1f);
            model = modeloSteve;
        }
        glUniform1i(glGetUniformLocation(lightingShader.Program, "useBones"), 0);

        lampShader.Use();

        modelLoc = glGetUniformLocation(lampShader.Program, "model");
        viewLoc = glGetUniformLocation(lampShader.Program, "view");
        projLoc = glGetUniformLocation(lampShader.Program, "projection");

        glUniformMatrix4fv(viewLoc, 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
        glUniformMatrix4fv(modelLoc, 1, GL_FALSE, glm::value_ptr(model));
        glBindVertexArray(0);

        glDepthFunc(GL_LEQUAL);
        skyboxShader.Use();
        view = glm::mat4(glm::mat3(camera.GetViewMatrix()));
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader.Program, "view"), 1, GL_FALSE, glm::value_ptr(view));
        glUniformMatrix4fv(glGetUniformLocation(skyboxShader.Program, "projection"), 1, GL_FALSE, glm::value_ptr(projection));

        glBindVertexArray(skyboxVAO);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
        glDepthFunc(GL_LESS);

        glfwSwapBuffers(window);
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &skyboxVBO);
    hacha.liberar();
    delete g_SteveAnim;
    delete g_SteveAnimator;
    glfwTerminate();
    return 0;
}

void DoMovement()
{

    if (keys[GLFW_KEY_W] || keys[GLFW_KEY_UP]) camera.ProcessKeyboard(FORWARD, deltaTime);
    if (keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN]) camera.ProcessKeyboard(BACKWARD, deltaTime);
    if (keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT]) camera.ProcessKeyboard(LEFT, deltaTime);
    if (keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT]) camera.ProcessKeyboard(RIGHT, deltaTime);

}

void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mode)
{
    if (key == GLFW_KEY_L && action == GLFW_PRESS)
    {
        luzNaranja = !luzNaranja;
        std::cout << "Luz direccional: " << (luzNaranja ? "naranja" : "blanca") << std::endl;
    }
    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        luzRojaActiva = !luzRojaActiva;
        std::cout << (luzRojaActiva ? "Luz roja activada" : "Luz roja desactivada") << std::endl;
    }
    if (key == GLFW_KEY_N && action == GLFW_PRESS)
    {
        luzAzulActiva = !luzAzulActiva;
        std::cout << (luzAzulActiva ? "Luz azul activada" : "Luz azul desactivada") << std::endl;
    }
    if (key == GLFW_KEY_Z && action == GLFW_PRESS)
    {
        zorroCaminando = !zorroCaminando;
        std::cout << "Zorro: " << (zorroCaminando ? "caminando" : "detenido") << std::endl;
    }
    if (key == GLFW_KEY_X && action == GLFW_PRESS && !zorroSaltando)
    {
        zorroSaltando = true;
        zorroTiempoSalto = 0.0f;
        std::cout << "Salto del zorro" << std::endl;
    }

    if (key == GLFW_KEY_C && action == GLFW_PRESS)
    {
        if (!explota)
        {
            pausa = true;
            std::cout << "Creeper: caminata pausada" << std::endl;
        }
    }
    if (key == GLFW_KEY_V && action == GLFW_PRESS)
    {
        if (!explota)
        {
            pausa = false;
            creeperCaminando = true;
            std::cout << "Creeper: caminata reanudada" << std::endl;
        }
    }
    if (key == GLFW_KEY_E && action == GLFW_PRESS)
    {
        if (!explota)
        {
            explota = true;
            pausa = true;
            creeperCaminando = false;
            creeperVisible = true;

            escalaActual = escalaInicial;
            movimientoVertical = 0.0f;
            balanceo = 0.0f;
            tiempoDesaparicion = 0.0f;

            std::cout << "Creeper: explosion iniciada" << std::endl;
        }
    }

    if (key == GLFW_KEY_H && action == GLFW_PRESS && !caballoRelinchando)
    {
        caballoRelinchando = true;
        caballoTiempo = 0.0f;
        std::cout << "Caballo relinchando" << std::endl;
    }
    if (key == GLFW_KEY_G && action == GLFW_PRESS && !hachaGolpeando)
    {
        hachaGolpeando = true;
        hachaTiempo = 0.0f;
        std::cout << "Habitantes golpeando con el hacha" << std::endl;
    }
    if (key == GLFW_KEY_O && action == GLFW_PRESS && !ovejaComiendo)
    {
        ovejaComiendo = true;
        ovejaTiempo = 0.0f;
        std::cout << "Oveja comiendo" << std::endl;
    }

    if (key == GLFW_KEY_B && action == GLFW_PRESS)
    {
        stevePosZ = 0.0f;
        steveEstado = 0;
        steveDir = 1.0f;

        g_SteveAnimator->PlayAnimation(g_SteveAnim);
        playFBX = true;

        printf("Animacion de Steve iniciada\n");

    }
    if (GLFW_KEY_ESCAPE == key && GLFW_PRESS == action)
    {
        glfwSetWindowShouldClose(window, GL_TRUE);
    }
    if (key >= 0 && key < 1024)
    {
        if (action == GLFW_PRESS)
            keys[key] = true;
        else if (action == GLFW_RELEASE)
            keys[key] = false;
    }
}

void AnimacionCreeper(float dt)
{
    if (!explota)
    {
        if (creeperCaminando && !pausa)
        {
            posicionActualZ += direccionMovimiento * velocidadMovimiento * dt;

            if (posicionActualZ >= limiteFinal)
            {
                posicionActualZ = limiteFinal;
                direccionMovimiento = -1.0f;
            }
            else if (posicionActualZ <= limiteInicial)
            {
                posicionActualZ = limiteInicial;
                direccionMovimiento = 1.0f;
            }

            rotacionCreeper = (direccionMovimiento > 0.0f) ? 0.0f : 180.0f;

            float tiempoActual = static_cast<float>(glfwGetTime());
            movimientoVertical = std::sin(tiempoActual * 8.0f) * 0.03f;
            balanceo = std::sin(tiempoActual * 8.0f) * 3.0f;
        }
        else
        {
            movimientoVertical *= 0.90f;
            balanceo *= 0.90f;
        }
    }
    else
    {
        if (creeperVisible)
        {
            escalaActual += velocidadExplosion * dt;
            rotacionCreeper += 240.0f * dt;
            movimientoVertical = std::sin(static_cast<float>(glfwGetTime()) * 30.0f) * 0.04f;
            balanceo = std::sin(static_cast<float>(glfwGetTime()) * 25.0f) * 8.0f;

            if (rotacionCreeper >= 360.0f) rotacionCreeper = 0.0f;

            if (escalaActual >= escalaMaxima)
            {
                creeperVisible = false;
                tiempoDesaparicion = 0.0f;
            }
        }
        else
        {
            tiempoDesaparicion += dt;

            if (tiempoDesaparicion >= duracionDesaparicion)
            {
                creeperVisible = true;
                explota = false;
                pausa = false;
                creeperCaminando = true;

                posicionActualX = posicionInicialX;
                posicionActualZ = posicionInicialZ;

                escalaActual = escalaInicial;
                movimientoVertical = 0.0f;
                balanceo = 0.0f;
                rotacionCreeper = 0.0f;
                tiempoDesaparicion = 0.0f;
                direccionMovimiento = 1.0f;
            }
        }
    }
}

// Animaciones activadas con teclas.
void AnimacionesExtra(float dt)
{
    if (caballoRelinchando)
    {
        caballoTiempo += dt;
        float p = caballoTiempo / caballoDuracion;
        if (p >= 1.0f)
        {
            caballoRelinchando = false;
            caballoTiempo = 0.0f;
            anguloCaballo = 0.0f;
        }
        else
        {
            float a = std::sin(3.14159265f * p) * 1.6f;
            if (a > 1.0f) a = 1.0f;
            anguloCaballo = anguloRelincho * a;
        }
    }

    if (hachaGolpeando)
    {
        hachaTiempo += dt;
        float p = hachaTiempo / hachaDuracion;
        if (p >= 1.0f)
        {
            hachaGolpeando = false;
            hachaTiempo = 0.0f;
            hachaAngulo = 0.0f;
        }
        else
        {
            float q = std::fmod(p * hachaGolpes, 1.0f);
            if (q < 0.3f) hachaAngulo = 50.0f * (q / 0.3f);
            else if (q < 0.5f) hachaAngulo = 50.0f - 120.0f * ((q - 0.3f) / 0.2f);
            else hachaAngulo = -70.0f * (1.0f - (q - 0.5f) / 0.5f);
        }
    }

    if (ovejaComiendo)
    {
        ovejaTiempo += dt;
        float p = ovejaTiempo / ovejaDuracion;
        if (p >= 1.0f)
        {
            ovejaComiendo = false;
            ovejaTiempo = 0.0f;
            ovejaAngulo = 0.0f;
        }
        else
        {
            float b = 1.0f;
            if (p < 0.15f) b = p / 0.15f;
            else if (p > 0.85f) b = (1.0f - p) / 0.15f;
            b = b * b * (3.0f - 2.0f * b);
            float masticar = (p > 0.15f && p < 0.85f)
                ? 4.0f * std::fabs(std::sin(ovejaTiempo * 10.0f)) : 0.0f;
            ovejaAngulo = (ovejaAnguloMax - masticar) * b;
        }
    }
}

void AnimationKeys()
{
    float dt = deltaTime;
    if (dt > 0.05f) dt = 0.05f;
    if (dt < 0.0f) dt = 0.0f;

    AnimacionCreeper(dt);
    AnimacionesExtra(dt);

    zorroAnguloPatas = 0.0f;
    zorroAnguloCola = 0.0f;
    zorroAnguloCabeza = 0.0f;

    // Animación de salto del zorro.
    if (zorroSaltando)
    {
        zorroTiempoSalto += dt;
        float avanceSalto = zorroTiempoSalto / zorroDuracionSalto;
        if (avanceSalto >= 1.0f)
        {
            zorroSaltando = false;
            zorroTiempoSalto = 0.0f;
            zorroElevacion = 0.0f;
        }
        else
        {
            zorroElevacion = 4.0f * zorroAlturaSalto * avanceSalto * (1.0f - avanceSalto);
            zorroAnguloPatas = -20.0f * std::sin(3.14159265f * avanceSalto);
            zorroAnguloCola = 10.0f * std::sin(3.14159265f * avanceSalto);
        }
        return;
    }
    zorroElevacion = 0.0f;

    // Animación de caminata del zorro.
    if (zorroCaminando)
    {
        zorroTiempoPasos += dt;
        zorroRecorrido = std::fmod(zorroRecorrido + zorroVelocidad * dt,
            zorroDistancia * 2.0f);
        if (zorroRecorrido < zorroDistancia)
        {
            zorroPosZ = zorroInicioZ + zorroRecorrido;
            zorroGiro = 0.0f;
        }
        else
        {
            zorroPosZ = zorroInicioZ + zorroDistancia * 2.0f - zorroRecorrido;
            zorroGiro = 180.0f;
        }
        zorroAnguloPatas = 25.0f * std::sin(zorroTiempoPasos * 8.0f);
        zorroAnguloCola = 12.0f * std::sin(zorroTiempoPasos * 4.0f);
        zorroAnguloCabeza = 3.0f * std::sin(zorroTiempoPasos * 4.0f);
    }
}

void MouseCallback(GLFWwindow* window, double xPos, double yPos)
{
    if (firstMouse)
    {
        lastX = xPos;
        lastY = yPos;
        firstMouse = false;
    }

    GLfloat xOffset = static_cast<GLfloat>(xPos - lastX);
    GLfloat yOffset = static_cast<GLfloat>(lastY - yPos);

    lastX = xPos;
    lastY = yPos;

    camera.ProcessMouseMovement(xOffset, yOffset);
}

void interpolation(void)
{

    KeyFrame[playIndex].incX = (KeyFrame[playIndex + 1].gatoPosX - KeyFrame[playIndex].gatoPosX) / i_max_steps;
    KeyFrame[playIndex].incY = (KeyFrame[playIndex + 1].gatoPosY - KeyFrame[playIndex].gatoPosY) / i_max_steps;
    KeyFrame[playIndex].incZ = (KeyFrame[playIndex + 1].gatoPosZ - KeyFrame[playIndex].gatoPosZ) / i_max_steps;
    KeyFrame[playIndex].headIncGato = (KeyFrame[playIndex + 1].headGato - KeyFrame[playIndex].headGato) / i_max_steps;
    KeyFrame[playIndex].FLegsIncGato = (KeyFrame[playIndex + 1].FLegsGato - KeyFrame[playIndex].FLegsGato) / i_max_steps;
    KeyFrame[playIndex].BLegsIncGato = (KeyFrame[playIndex + 1].BLegsGato - KeyFrame[playIndex].BLegsGato) / i_max_steps;
    KeyFrame[playIndex].tailIncGato = (KeyFrame[playIndex + 1].tailGato - KeyFrame[playIndex].tailGato) / i_max_steps;

    KeyFrame[playIndex].rotGatoInc = (KeyFrame[playIndex + 1].rotGato - KeyFrame[playIndex].rotGato) / i_max_steps;
    KeyFrame[playIndex].inclinacionIncGato = (KeyFrame[playIndex + 1].inclinacionGato - KeyFrame[playIndex].inclinacionGato) / i_max_steps;
}
