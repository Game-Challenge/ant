﻿#include <binding/render.h>
#include <binding/render.h>
#include <binding/utf8.h>
#include <binding/context.h>
#include <core/Core.h>
#include <core/Interface.h>
#include <core/File.h>
#include <memory.h>
#include <cassert>
#include <stdint.h>
#include "../bgfx/bgfx_interface.h"
#include "../core/Color.h"

extern "C" {
    #include <textureman.h>
    #include "../font/font_manager.h"
}

#ifdef RMLUI_MATRIX_ROW_MAJOR
error "need matrix type as column major"
#endif //RMLUI_MATRIX_ROW_MAJOR

#define RENDER_STATE (BGFX_STATE_WRITE_RGB|BGFX_STATE_DEPTH_TEST_ALWAYS|BGFX_STATE_BLEND_ALPHA|BGFX_STATE_MSAA)

typedef unsigned int utfint;
#define MAXUNICODE	0x10FFFFu
#define MAXUTF		0x7FFFFFFFu
#define FIXPOINT FONT_POSTION_FIX_POINT

const char* utf8_decode1(const char* s, utfint* val, int strict,int& cnt) {

    static const utfint limits[] =
    { ~(utfint)0, 0x80, 0x800, 0x10000u, 0x200000u, 0x4000000u };
    unsigned int c = (unsigned char)s[0];
    utfint res = 0;  /* final result */
    if(c == '`'){
        *val = 0x8000000u;
        cnt  = 1;
        return s + 1;
    }
    if (c < 0x80)  /* ascii? */
        res = c,cnt=0;
    else {
        int count = 0;  /* to count number of continuation bytes */
        for (; c & 0x40; c <<= 1) {  /* while it needs continuation bytes... */
            unsigned int cc = (unsigned char)s[++count];  /* read next byte */
            if ((cc & 0xC0) != 0x80)  /* not a continuation byte? */
                return NULL;  /* invalid byte sequence */
            res = (res << 6) | (cc & 0x3F);  /* add lower 6 bits from cont. byte */
        }
        res |= ((utfint)(c & 0x7F) << (count * 5));  /* add first byte */
        if (count > 5 || res > MAXUTF || res < limits[count])
            return NULL;  /* invalid byte sequence */
        s += count;  /* skip continuation bytes read */
        cnt=count;
    }
    if (strict) {
        /* check for invalid code points; too large or surrogates */
        if (res > MAXUNICODE || (0xD800u <= res && res <= 0xDFFFu))
            return NULL;
    }
    if (val) *val = res;
    cnt+=1;
    return s + 1;  /* +1 to include first byte */
}

static uint32_t getTextureFlags(Rml::SamplerFlag flags) {
    switch (flags) {
    default:
    case Rml::SamplerFlag::Unset:
        return UINT32_MAX;
    case Rml::SamplerFlag::Repeat:
        return 0;
    case Rml::SamplerFlag::RepeatX:
        return BGFX_SAMPLER_V_BORDER;
    case Rml::SamplerFlag::RepeatY:
        return BGFX_SAMPLER_U_BORDER;
    case Rml::SamplerFlag::NoRepeat:
        return BGFX_SAMPLER_U_BORDER | BGFX_SAMPLER_V_BORDER;
    }
}

class TextureUniform {
public:
    TextureUniform(uint16_t id, uint16_t tex)
        : id(id)
        , tex(tex)
    {}
    void Submit(bgfx_encoder_t* encoder, uint32_t flags = UINT32_MAX) {
        BGFX(encoder_set_texture)(encoder, 0, {id}, {tex}, flags);
    }
private:
    uint16_t id;
    uint16_t tex;
};

class AsyncTextureUniform {
public:
    AsyncTextureUniform(uint16_t id, Rml::TextureId tex)
        : id(id)
        , tex(tex)
    {}
    void Submit(bgfx_encoder_t* encoder, uint32_t flags = UINT32_MAX) {
        bgfx_texture_handle_t handle = texture_get(tex);
        BGFX(encoder_set_texture)(encoder, 0, {id}, handle, flags);
    }
private:
    uint16_t id;
    Rml::TextureId tex;
};

class Uniform {
public:
    Uniform(uint16_t id)
        : id(id)
    {}
    void Submit(bgfx_encoder_t* encoder, float v0, float v1 = 0.f, float v2 = 0.f, float v3 = 0.f) {
        glm::vec4 vec = {v0, v1, v2, v3};
        BGFX(encoder_set_uniform)(encoder, {id}, &vec, 1);
    }
    void Submit(bgfx_encoder_t* encoder, glm::vec4 vec[2]) {
        BGFX(encoder_set_uniform)(encoder, {id}, vec, 2);
    }
private:
    uint16_t id;
};

class ColorUniform: public Uniform {
public:
    ColorUniform(uint16_t id, Rml::Color color)
        : Uniform(id)
        , color(color)
    {}
    void Submit(bgfx_encoder_t* encoder) {
        Uniform::Submit(encoder, 
            color.r / 255.f,
            color.g / 255.f,
            color.b / 255.f,
            color.a / 255.f
        );
    }
private:
    Rml::Color color;
};

class Material {
public:
    virtual ~Material() {};
    virtual void     Submit(bgfx_encoder_t* encoder) = 0;
    virtual uint16_t Program(const RenderState& state, const shader& s) = 0;
};

static uint16_t _FindTextureProgram(const RenderState &state, const shader& s){
    if (state.needShaderClipRect){
        return state.needGray ? s.image_cr_gray : s.image_cr;
    }

    return state.needGray ? s.image_gray : s.image;
}

class TextureMaterial: public Material {
public:
    TextureMaterial(shader const& s, bgfx_texture_handle_t tex, Rml::SamplerFlag flags)
        : tex_uniform(s.find_uniform("s_tex"), tex.idx)
        , flags(getTextureFlags(flags))
    { }
    void Submit(bgfx_encoder_t* encoder) override {
        tex_uniform.Submit(encoder, flags);
    }
    uint16_t Program(const RenderState& state, const shader& s) override {
        return _FindTextureProgram(state, s);
    }
private:
    TextureUniform tex_uniform;
    uint32_t flags;
};

class AsyncTextureMaterial: public Material {
public:
    AsyncTextureMaterial(shader const& s, Rml::TextureId texid, Rml::SamplerFlag flags)
        : tex_uniform(s.find_uniform("s_tex"), texid)
        , flags(getTextureFlags(flags))
    { }
    void Submit(bgfx_encoder_t* encoder) override {
        tex_uniform.Submit(encoder, flags);
    }
    uint16_t Program(const RenderState& state, const shader& s) override {
        return _FindTextureProgram(state, s);
    }
private:
    AsyncTextureUniform tex_uniform;
    uint32_t flags;
};

class TextMaterial: public Material {
public:
    TextMaterial(const shader& s, struct font_manager* F, uint16_t texid, int8_t edgeValueOffset = 0, float width = 0.f)
        : tex_uniform(s.find_uniform("s_tex"), texid)
        , mask_uniform(s.find_uniform("u_mask"))
        , mask_0(F->font_manager_sdf_mask(F) - F->font_manager_sdf_distance(F, edgeValueOffset))
        , mask_2(width)
    { }
    void Submit(bgfx_encoder_t* encoder) override {
        tex_uniform.Submit(encoder);
        const float distMultiplier = 1.f;
        mask_uniform.Submit(encoder, mask_0, distMultiplier, mask_2);
    }
    uint16_t Program(const RenderState& state, const shader& s) override {
        return state.needShaderClipRect
            ? s.font_cr
            : s.font
            ;
    }
protected:
    TextureUniform tex_uniform;
    Uniform mask_uniform;
    float mask_0;
    float mask_2;
};

class TextStrokeMaterial: public TextMaterial {
public:
    TextStrokeMaterial(const shader& s, struct font_manager* F, uint16_t texid, int8_t edgeValueOffset, Rml::Color color, float width)
        : TextMaterial(s, F, texid, edgeValueOffset, width)
        , color_uniform(s.find_uniform("u_effect_color"), color)
    {}
    void Submit(bgfx_encoder_t* encoder) override {
        TextMaterial::Submit(encoder);
        color_uniform.Submit(encoder);
    }
    uint16_t Program(const RenderState& state, const shader& s) override {
        return state.needShaderClipRect
            ? s.font_outline_cr
            : s.font_outline
            ;
    }
protected:
    ColorUniform color_uniform;
};

class TextShadowMaterial: public TextMaterial {
public:
    TextShadowMaterial(const shader& s, struct font_manager* F, uint16_t texid, int8_t edgeValueOffset, Rml::Color color, Rml::Point offset)
        : TextMaterial(s, F, texid, edgeValueOffset)
        , color_uniform(s.find_uniform("u_effect_color"), color)
        , offset_uniform(s.find_uniform("u_shadow_offset"))
        , offset(offset)
    {}
    void Submit(bgfx_encoder_t* encoder) override {
        TextMaterial::Submit(encoder);
        color_uniform.Submit(encoder);
        offset_uniform.Submit(encoder, offset.x / FONT_MANAGER_TEXSIZE, offset.y / FONT_MANAGER_TEXSIZE);
    }
    uint16_t Program(const RenderState& state, const shader& s) override {
        return state.needShaderClipRect
            ? s.font_shadow_cr
            : s.font_shadow
            ;
    }
protected:
    ColorUniform color_uniform;
    Uniform offset_uniform;
    Rml::Point offset;
};

static bgfx_texture_handle_t CreateDefaultTexture() {
    const bgfx_memory_t* mem = BGFX(alloc)(4);
    *(uint32_t*)mem->data = 0xFFFFFFFF;
    bgfx_texture_handle_t h = BGFX(create_texture_2d)(1, 1, false, 1, BGFX_TEXTURE_FORMAT_RGBA8, BGFX_TEXTURE_SRGB, mem);
    return h;
}

Renderer::Renderer(const RmlContext* context)
    : mcontext(context)
    , mEncoder(nullptr)
    , default_tex(CreateDefaultTexture())
    , default_tex_mat(std::make_unique<TextureMaterial>(
        context->shader,
        default_tex,
        Rml::SamplerFlag::Unset
    ))
    , default_font_mat(std::make_unique<TextMaterial>(
        context->shader,
        context->font_mgr,
        context->font_tex.texid
    ))
    , clip_uniform(std::make_unique<Uniform>(
        context->shader.find_uniform("u_clip_rect")
    ))
{
    BGFX(set_view_mode)(context->viewid, BGFX_VIEW_MODE_SEQUENTIAL);
    Rml::SetRenderInterface(this);
}

Renderer::~Renderer() {
    BGFX(destroy_texture)({default_tex});
}

void Renderer::RenderGeometry(Rml::Vertex* vertices, size_t num_vertices, Rml::Index* indices, size_t num_indices, Rml::MaterialHandle mat) {
    BGFX(encoder_set_state)(mEncoder, RENDER_STATE, 0);
    bgfx_transient_vertex_buffer_t tvb;
    BGFX(alloc_transient_vertex_buffer)(&tvb, (uint32_t)num_vertices, (bgfx_vertex_layout_t*)mcontext->layout);

    memcpy(tvb.data, vertices, num_vertices * sizeof(Rml::Vertex));
    BGFX(encoder_set_transient_vertex_buffer)(mEncoder, 0, &tvb, 0, (uint32_t)num_vertices);

    bgfx_transient_index_buffer_t tib;
    BGFX(alloc_transient_index_buffer)(&tib, (uint32_t)num_indices, true);

    static_assert(sizeof(Rml::Index) == sizeof(uint32_t));
    memcpy(tib.data, indices, num_indices * sizeof(Rml::Index));
    BGFX(encoder_set_transient_index_buffer)(mEncoder, &tib, 0, (uint32_t)num_indices);

    submitScissorRect(mEncoder);

    Material* material = reinterpret_cast<Material*>(mat);
    material->Submit(mEncoder);

    auto prog = material->Program(state, mcontext->shader);
    const uint8_t discard_flags = ~BGFX_DISCARD_TRANSFORM;
    BGFX(encoder_submit)(mEncoder, mcontext->viewid, { prog }, 0, discard_flags);
}

void Renderer::Begin() {
    mEncoder = BGFX(encoder_begin)(false);
    assert(mEncoder);
}

void Renderer::End() {
    BGFX(encoder_end)(mEncoder);
}

#ifdef _DEBUG
void Renderer::drawDebugScissorRect(bgfx_encoder_t *encoder, uint16_t viewid, uint16_t progid){
    if (!state.needShaderClipRect)
        return;

    glm::mat4 m(1.f);
    BGFX(encoder_set_transform)(encoder, &m, 1);

    static bgfx_vertex_layout_t debugLayout;
    static bool isinit = false;
    if (!isinit){
        isinit = true;
        BGFX(vertex_layout_begin)(&debugLayout, BGFX_RENDERER_TYPE_COUNT);
        BGFX(vertex_layout_add)(&debugLayout, BGFX_ATTRIB_POSITION, 2, BGFX_ATTRIB_TYPE_FLOAT, false, false);
        BGFX(vertex_layout_end)(&debugLayout);
    }
    
    bgfx_transient_vertex_buffer_t tvb;
    BGFX(alloc_transient_vertex_buffer)(&tvb, 4, &debugLayout);

    memcpy(tvb.data, &state.rectVerteices, sizeof(glm::vec2)*4);
    BGFX(encoder_set_transient_vertex_buffer)(encoder, 0, &tvb, 0, 4);

    bgfx_transient_index_buffer_t tib;
    BGFX(alloc_transient_index_buffer)(&tib, 6, false);

    const uint16_t indices[] = {0, 1, 2, 1, 3, 2};
    BGFX(encoder_set_transient_index_buffer)(encoder, &tib, 0, 6);

    BGFX(encoder_set_state)(encoder, RENDER_STATE, 0);

    BGFX(encoder_submit)(encoder, viewid, {progid}, 0, BGFX_DISCARD_ALL);
}
#endif //_DEBUG

void Renderer::setShaderScissorRect(bgfx_encoder_t* encoder, const glm::vec4 r[2]){
    state.needShaderClipRect = true;
    state.lastScissorId = UINT16_MAX;
    state.rectVerteices[0] = r[0];
    state.rectVerteices[1] = r[1];
    BGFX(encoder_set_scissor_cached)(encoder, UINT16_MAX);
}

void Renderer::setScissorRect(bgfx_encoder_t* encoder, const glm::u16vec4 *r) {
    state.needShaderClipRect = false;
    if (r == nullptr){
        state.lastScissorId = UINT16_MAX;
        BGFX(encoder_set_scissor_cached)(encoder, UINT16_MAX);
    } else {
        state.lastScissorId = BGFX(encoder_set_scissor)(encoder, r->x, r->y, r->z, r->w);
    }
}

void Renderer::submitScissorRect(bgfx_encoder_t* encoder){
    if (state.needShaderClipRect) {
        clip_uniform->Submit(encoder, state.rectVerteices);
    } else {
        BGFX(encoder_set_scissor_cached)(encoder, state.lastScissorId);
    }
}

void Renderer::SetTransform(const glm::mat4x4& transform) {
    BGFX(encoder_set_transform)(mEncoder, &transform, 1);
}

void Renderer::SetClipRect() {
    setScissorRect(mEncoder, nullptr);
}

void Renderer::SetClipRect(const glm::u16vec4& r) {
    setScissorRect(mEncoder, &r);
}

void Renderer::SetClipRect(glm::vec4 r[2]) {
    setShaderScissorRect(mEncoder, r);
}

void Renderer::SetGray(bool enable) {
    state.needGray = enable;
}

Rml::MaterialHandle Renderer::CreateTextureMaterial(Rml::TextureId texture, Rml::SamplerFlag flags) {
    auto material = std::make_unique<AsyncTextureMaterial>(mcontext->shader, texture, flags);
    return reinterpret_cast<Rml::MaterialHandle>(material.release());
}

Rml::MaterialHandle Renderer::CreateRenderTextureMaterial(Rml::TextureId texture, Rml::SamplerFlag flags) {
    auto material = std::make_unique<TextureMaterial>(mcontext->shader, bgfx_texture_handle_t{texture}, flags);
    return reinterpret_cast<Rml::MaterialHandle>(material.release());
} 

struct TextEffectVisitor {
    const RmlContext* context;
    Rml::MaterialHandle operator() (Rml::TextStroke const& t) {
        font_manager* F = context->font_mgr;
        int8_t edgevalueOffset = int8_t(F->font_manager_sdf_mask(F) * 0.85f);
        auto material = std::make_unique<TextStrokeMaterial>(
            context->shader,
            F,
            context->font_tex.texid,
            edgevalueOffset,
            t.color,
            t.width
        );
        return reinterpret_cast<Rml::MaterialHandle>(material.release());
    }
    Rml::MaterialHandle operator() (Rml::TextShadow const& t) {
        font_manager* F = context->font_mgr;
        int8_t edgevalueOffset = int8_t(F->font_manager_sdf_mask(F) * 0.85f);
        auto material = std::make_unique<TextShadowMaterial>(
            context->shader,
            F,
            context->font_tex.texid,
            edgevalueOffset,
            t.color,
            Rml::Point(t.offset_h, t.offset_v)
        );
        return reinterpret_cast<Rml::MaterialHandle>(material.release());
    }
};

Rml::MaterialHandle Renderer::CreateFontMaterial(const Rml::TextEffects& effects) {
    if (effects.empty()) {
        return reinterpret_cast<Rml::MaterialHandle>(default_font_mat.get());
    }
    if (effects.size() != 1){
        assert(false && "not support more than one font effect in single text");
        return reinterpret_cast<Rml::MaterialHandle>(default_font_mat.get());
    }
    return std::visit(TextEffectVisitor{mcontext}, effects[0]);
}

Rml::MaterialHandle Renderer::CreateDefaultMaterial() {
     return reinterpret_cast<Rml::MaterialHandle>(default_tex_mat.get());
}

void Renderer::DestroyMaterial(Rml::MaterialHandle mat) {
    Material* material = reinterpret_cast<Material*>(mat);
    if (default_font_mat.get() != material && default_tex_mat.get() != material) {
        delete material;
    }
}


union FontFace {
	struct {
		uint32_t fontid;
		uint32_t pixelsize;
	};
	uint64_t handle;
};

Rml::FontFaceHandle Renderer::GetFontFaceHandle(const std::string& family, Rml::Style::FontStyle style, Rml::Style::FontWeight weight, uint32_t size) {
    font_manager* F = mcontext->font_mgr;
    int fontid = F->font_manager_addfont_with_family(F, family.c_str());
    if (fontid <= 0) {
        return static_cast<Rml::FontFaceHandle>(0);
    }
    FontFace face;
    face.fontid = (uint32_t)fontid;
    face.pixelsize = size;
    return face.handle;
}

static bool UpdateTexture(bgfx_texture_handle_t th, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t *buffer) {
    if (!BGFX_HANDLE_IS_VALID(th))
        return false;
    const uint32_t bytes = (uint32_t)w * h;
    auto mem = BGFX(make_ref)(buffer, bytes);
    BGFX(update_texture_2d)(th, 0, 0, x, y, w, h, mem, w);
    return true;
}

static struct font_glyph GetGlyph(const RmlContext* mcontext, const FontFace& face, int codepoint, struct font_glyph* og_ = nullptr) {
    struct font_glyph g, og;
    font_manager* F = mcontext->font_mgr;
    if (0 == F->font_manager_glyph(F, face.fontid, codepoint, face.pixelsize, &g, &og)) {
        const uint32_t bufsize = og.w * og.h;
        uint8_t *buffer = new uint8_t[bufsize];
        memset(buffer, 0, bufsize);
        if (NULL == F->font_manager_update(F, face.fontid, codepoint, &og, buffer)) {
            UpdateTexture({(uint16_t)mcontext->font_tex.texid}, og.u, og.v, og.w, og.h, buffer);
        }
        else {
            delete[] buffer;
        }
    }
    if (og_)
        *og_ = og;
    return g;
}

void Renderer::GetHeight(Rml::FontFaceHandle handle, int& ascent, int& descent, int& lineGap) {
    font_manager* F = mcontext->font_mgr;
    FontFace face;
    face.handle = handle;
    F->font_manager_fontheight(F, face.fontid, face.pixelsize, &ascent, &descent, &lineGap);
}

void Renderer::GetUnderline(Rml::FontFaceHandle handle, float& position, float &thickness){
    font_manager* F = mcontext->font_mgr;
    FontFace face;
    face.handle = handle;
    F->font_manager_underline(F, face.fontid, face.pixelsize, &position, &thickness);
}

float Renderer::GetStringWidth(Rml::FontFaceHandle handle, const std::string& string){
    FontFace face;
    face.handle = handle;
    float width = 0;
    for (auto c : utf8::view(string)) {
        auto glyph = GetGlyph(mcontext, face, c);
        width += (float)glyph.advance_x;
    }
    return width;
}

float Renderer::GetRichStringWidth(Rml::FontFaceHandle handle, const std::string& string, std::vector<Rml::image>& images, int& cur_image_idx, float line_height){
    FontFace face;
    face.handle = handle;
    float width = 0;
    for (auto c : utf8::view(string)) {
        if(c == '`'){
            width += line_height;
            cur_image_idx++;
        }
        else{
            auto glyph = GetGlyph(mcontext, face, c);
            width += (float)glyph.advance_x;
        }
    }
    return width;
}

// why 32768, which want to use vs_uifont.sc shader to render font
// and vs_uifont.sc also use in runtime font render.
// the runtime font renderer store vertex position in int16
// when it pass to shader, it convert from int16, range from: [-32768, 32768], to [-1.0, 1.0]
// why store in uint16 ? because bgfx not support ....
#define MAGIC_FACTOR    32768.f

void Renderer::GenerateString(Rml::FontFaceHandle handle, Rml::LineList& lines, const Rml::Color& color, Rml::Geometry& geometry){
    auto& vertices = geometry.GetVertices();
    auto& indices = geometry.GetIndices();
    vertices.clear();
    indices.clear();
    for (size_t i = 0; i < lines.size(); ++i) {
        Rml::Line& line = lines[i];
        vertices.reserve(vertices.size() + line.text.size() * 4);
        indices.reserve(indices.size() + line.text.size() * 6);

        FontFace face;
        face.handle = handle;
        const Rml::Point fonttexel(1.f / mcontext->font_tex.width, 1.f / mcontext->font_tex.height);

        int x = int(line.position.x + 0.5f), y = int(line.position.y + 0.5f);
        for (auto codepoint : utf8::view(line.text)) {

            struct font_glyph og;
            auto g = GetGlyph(mcontext, face, codepoint, &og);

            // Generate the geometry for the character.
            const int x0 = x + g.offset_x;
            const int y0 = y + g.offset_y;
            const int16_t u0 = g.u;
            const int16_t v0 = g.v;

            const float scale = FONT_POSTION_FIX_POINT / MAGIC_FACTOR;
            geometry.AddRectFilled(
                { x0 * scale, y0 * scale, g.w * scale, g.h * scale },
                { u0 * fonttexel.x, v0 * fonttexel.y ,og.w * fonttexel.x , og.h * fonttexel.y },
                color
            );

            //x += g.advance_x + (dim.x - olddim.x);
            x += g.advance_x;
        }

        line.width = x - int(line.position.x + 0.5f);
    }
}

void Renderer::GenerateRichString(Rml::FontFaceHandle handle, Rml::LineList& lines, std::vector<uint32_t>& codepoints, Rml::Geometry& textgeometry, std::vector<std::unique_ptr<Rml::Geometry>> & imagegeometries, std::vector<Rml::image>& images, int& cur_image_idx, float line_height){
    auto& vertices = textgeometry.GetVertices();
    auto& indices = textgeometry.GetIndices();
    vertices.clear();
    indices.clear();
    for (size_t i = 0; i < lines.size(); ++i) {
        Rml::Line& line = lines[i];
        vertices.reserve(vertices.size() + line.text.size() * 4);
        indices.reserve(indices.size() + line.text.size() * 6);

        FontFace face;
        face.handle = handle;
        const Rml::Point fonttexel(1.f / mcontext->font_tex.width, 1.f / mcontext->font_tex.height);

        float x = line.position.x + 0.5f, y = line.position.y + 0.5f;
        
        Rml::Color color;
        for (auto& layout:line.layouts){
            for(int ii=0;ii<layout.num;++ii){
                uint32_t codepoint=codepoints[layout.start+ii];
                if(codepoint == 0x8000000u){
                    color.r = color.g = color.b = 255;
                    auto& image_vertices = imagegeometries[cur_image_idx]->GetVertices();
                    auto& image_indices = imagegeometries[cur_image_idx]->GetIndices();
                    image_vertices.clear();
                    image_indices.clear();
                    image_vertices.reserve(4);
                    image_indices.reserve(6); 
                    float u0 = images[cur_image_idx].rect.origin.x/images[cur_image_idx].width;
                    float v0 = images[cur_image_idx].rect.origin.y/images[cur_image_idx].height;
                    float w0 = images[cur_image_idx].rect.size.w/images[cur_image_idx].width;
                    float h0 = images[cur_image_idx].rect.size.h/images[cur_image_idx].height;
                    imagegeometries[cur_image_idx]->AddRectFilled(
                        //{ float(x + line_height * 0.1) , float(y - line_height * 0.8), float(line_height * 0.9) , float(line_height * 0.8)},
                        { float(x ) , float(y - line_height), float(line_height ) , float(line_height )},
                        { u0 , v0 , w0 , h0 },
                        color
                    );
                    cur_image_idx++;
                    x += line_height;
                }
                else{
                    color=layout.color;
                    struct font_glyph og;
                    auto g = GetGlyph(mcontext, face, codepoint, &og);

                    const float x0 = x + g.offset_x;
                    const float y0 = y + g.offset_y;
                    const int16_t u0 = g.u;
                    const int16_t v0 = g.v;

                    const float scale = FONT_POSTION_FIX_POINT / MAGIC_FACTOR;
                    textgeometry.AddRectFilled(
                        { x0 * scale, y0 * scale, g.w * scale, g.h * scale },
                        { u0 * fonttexel.x, v0 * fonttexel.y ,og.w * fonttexel.x , og.h * fonttexel.y },
                        color
                    );
                    x += g.advance_x;   
                }             
            }
        }
        line.width = int(x - (line.position.x + 0.5f));
    }

}

float Renderer::PrepareText(Rml::FontFaceHandle handle,const std::string& string,std::vector<uint32_t>& codepoints,std::vector<int>& groupmap,std::vector<Rml::group>& groups, std::vector<Rml::image>& images, std::vector<Rml::layout>& line_layouts,int start,int num){
    float line_width=0.f;

    FontFace face;
    face.handle = handle;

    Rml::layout l;
    uint16_t lstart=(uint16_t)codepoints.size();
    int lnum=0;
    Rml::Color pre_color,cur_color;
    int i=0;//i代表是当前string的位移 //start+i代表在ctext中的位移
    int cur_lm;

    if(num){
        int group_idx = groupmap[start+i];
        if(group_idx>=100){
            pre_color=groups[0].color;
        }
        else{
            pre_color=groups[group_idx].color;
        }
        l.color=pre_color;
        l.start=lstart;

        uint32_t codepoint = 0;
        const char* str = (const char*)&string[i];
        int cnt=0;
        str=utf8_decode1(str, &codepoint, 1,cnt);
        codepoints.emplace_back(codepoint);
        assert(str);
        if(codepoint == 0x8000000u){
            line_width+=images[group_idx-100].rect.size.w;
        } 
        else{
            auto glyph=GetGlyph(mcontext,face,codepoint);
            line_width+=glyph.advance_x;
        }
        if(cnt>1){
            i+=3;
        }
        else{
            i+=1;
        }
        lstart++;
        lnum++;            
    }
    else return 0.f;

    while(i<num){
        int group_idx = groupmap[start+i];
        if(group_idx>=100){
            cur_lm = 0;
        }
        else{
            cur_lm=group_idx;
        }        
        cur_color=groups[cur_lm].color;
        if(!(cur_color==pre_color)){
            l.num=lnum;
            line_layouts.emplace_back(l);
            l.color=cur_color;
            l.start=lstart;
            lnum=0;
            pre_color=cur_color;
        }

        uint32_t codepoint = 0;
        const char* str = (const char*)&string[i];
        int cnt=0;
        str=utf8_decode1(str, &codepoint, 1,cnt);
        codepoints.emplace_back(codepoint);
        assert(str);
        if(codepoint == 0x8000000u){
            line_width+=images[group_idx-100].rect.size.w;
        } 
        else{
            auto glyph=GetGlyph(mcontext,face,codepoint);
            line_width+=glyph.advance_x;
        }
        if(cnt>1){
            i+=3;
        }
        else{
            i+=1;
        }
        lstart++;
        lnum++;                
    }

    l.num=lnum;
    line_layouts.emplace_back(l);
    return line_width;
}