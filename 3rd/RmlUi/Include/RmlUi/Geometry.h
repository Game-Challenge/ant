/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef RMLUI_CORE_GEOMETRY_H
#define RMLUI_CORE_GEOMETRY_H

#include "Header.h"
#include "Vertex.h"
#include <stdint.h>

namespace Rml {

class Context;
class Element;
class RenderInterface;
struct Texture;
using GeometryDatabaseHandle = uint32_t;

/**
	A helper object for holding an array of vertices and indices, and compiling it as necessary when rendered.

	@author Peter Curry
 */

class RMLUICORE_API Geometry {
public:
	Geometry();
	Geometry(const Geometry&) = delete;
	Geometry& operator=(const Geometry&) = delete;
	Geometry(Geometry&& other);
	Geometry& operator=(Geometry&& other);
	~Geometry();
	void Render(Vector2f translation);
	Vector< Vertex >& GetVertices();
	Vector< int >& GetIndices();
	const Texture* GetTexture() const;
	void SetTexture(const Texture* texture);
	void Release();
	void ReleaseCompiledGeometry();
	explicit operator bool() const;

private:
	void MoveFrom(Geometry& other);
	Vector< Vertex > vertices;
	Vector< int > indices;
	const Texture* texture = nullptr;
	CompiledGeometryHandle compiled_geometry = 0;
	bool compile_attempted = false;
	GeometryDatabaseHandle database_handle;
};

using GeometryList = Vector< Geometry >;

} // namespace Rml
#endif
