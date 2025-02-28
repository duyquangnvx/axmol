/****************************************************************************
Copyright (c) 2008-2010 Ricardo Quesada
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2011      Zynga Inc.
Copyright (c) 2013-2016 Chukong Technologies Inc.
Copyright (c) 2017-2018 Xiamen Yaji Software Co., Ltd.

https://axmol.dev/

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/
#include "2d/AtlasNode.h"
#include <stddef.h>  // offsetof
#include "base/Types.h"
#include "renderer/TextureAtlas.h"
#include "base/Director.h"
#include "renderer/TextureCache.h"
#include "base/Utils.h"
#include "renderer/Shaders.h"
#include "renderer/Renderer.h"
#include "renderer/backend/ProgramState.h"

namespace ax
{

// implementation AtlasNode

// AtlasNode - Creation & Init

AtlasNode::~AtlasNode()
{
    AX_SAFE_RELEASE(_textureAtlas);
}

AtlasNode* AtlasNode::create(std::string_view tile, int tileWidth, int tileHeight, int itemsToRender)
{
    AtlasNode* ret = new AtlasNode();
    if (ret->initWithTileFile(tile, tileWidth, tileHeight, itemsToRender))
    {
        ret->autorelease();
        return ret;
    }
    AX_SAFE_DELETE(ret);
    return nullptr;
}

bool AtlasNode::initWithTileFile(std::string_view tile, int tileWidth, int tileHeight, int itemsToRender)
{
    AXASSERT(!tile.empty(), "file size should not be empty");
    Texture2D* texture = _director->getTextureCache()->addImage(tile);
    return initWithTexture(texture, tileWidth, tileHeight, itemsToRender);
}

bool AtlasNode::initWithTexture(Texture2D* texture, int tileWidth, int tileHeight, int itemsToRender)
{
    _itemWidth  = tileWidth;
    _itemHeight = tileHeight;

    _colorUnmodified    = Color3B::WHITE;
    _isOpacityModifyRGB = true;

    _blendFunc = BlendFunc::ALPHA_PREMULTIPLIED;

    _textureAtlas = new TextureAtlas();

    _textureAtlas->initWithTexture(texture, itemsToRender);

    setProgramStateWithRegistry(backend::ProgramType::POSITION_TEXTURE_COLOR, texture);

    this->updateBlendFunc();
    this->updateOpacityModifyRGB();

    this->calculateMaxItems();

    _quadsToDraw = itemsToRender;

    return true;
}

bool AtlasNode::setProgramState(backend::ProgramState* programState, bool ownPS /*= false*/)
{
    if (Node::setProgramState(programState, ownPS))
    {
        auto& pipelineDescriptor        = _quadCommand.getPipelineDescriptor();
        pipelineDescriptor.programState = _programState;
        _mvpMatrixLocation              = _programState->getUniformLocation("u_MVPMatrix");

        updateProgramStateTexture(_textureAtlas->getTexture());
        return true;
    }
    return false;
}

// AtlasNode - Atlas generation

void AtlasNode::calculateMaxItems()
{
    Vec2 s = _textureAtlas->getTexture()->getContentSize();

    if (_ignoreContentScaleFactor)
    {
        s = _textureAtlas->getTexture()->getContentSizeInPixels();
    }

    _itemsPerColumn = (int)(s.height / _itemHeight);
    _itemsPerRow    = (int)(s.width / _itemWidth);
}

void AtlasNode::updateAtlasValues()
{
    AXASSERT(false, "CCAtlasNode:Abstract updateAtlasValue not overridden");
}

// AtlasNode - draw
void AtlasNode::draw(Renderer* renderer, const Mat4& transform, uint32_t flags)
{
    if (_textureAtlas->getTotalQuads() == 0)
        return;

    auto programState = _quadCommand.getPipelineDescriptor().programState;

    const auto& projectionMat = _director->getMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_PROJECTION);
    programState->setUniform(_mvpMatrixLocation, projectionMat.m, sizeof(projectionMat.m));

    _quadCommand.init(_globalZOrder, _textureAtlas->getTexture(), _blendFunc, _textureAtlas->getQuads(), _quadsToDraw,
                      transform, flags);
    renderer->addCommand(&_quadCommand);
}

// AtlasNode - RGBA protocol

const Color3B& AtlasNode::getColor() const
{
    if (_isOpacityModifyRGB)
    {
        return _colorUnmodified;
    }
    return Node::getColor();
}

void AtlasNode::setColor(const Color3B& color3)
{
    Color3B tmp      = color3;
    _colorUnmodified = color3;

    if (_isOpacityModifyRGB)
    {
        tmp.r = tmp.r * _displayedOpacity / 255;
        tmp.g = tmp.g * _displayedOpacity / 255;
        tmp.b = tmp.b * _displayedOpacity / 255;
    }
    Node::setColor(tmp);
}

void AtlasNode::setOpacity(uint8_t opacity)
{
    Node::setOpacity(opacity);

    // special opacity for premultiplied textures
    if (_isOpacityModifyRGB)
        this->setColor(_colorUnmodified);
}

void AtlasNode::setOpacityModifyRGB(bool value)
{
    Color3B oldColor    = this->getColor();
    _isOpacityModifyRGB = value;
    this->setColor(oldColor);
}

bool AtlasNode::isOpacityModifyRGB() const
{
    return _isOpacityModifyRGB;
}

void AtlasNode::updateOpacityModifyRGB()
{
    _isOpacityModifyRGB = _textureAtlas->getTexture()->hasPremultipliedAlpha();
}

void AtlasNode::setIgnoreContentScaleFactor(bool ignoreContentScaleFactor)
{
    if (_ignoreContentScaleFactor != ignoreContentScaleFactor)
    {
        _ignoreContentScaleFactor = ignoreContentScaleFactor;
        this->calculateMaxItems();
        this->updateAtlasValues();

        auto label = dynamic_cast<LabelProtocol*>(this);
        if (label)
        {
            Vec2 s = Vec2(static_cast<float>(label->getString().size() * _itemWidth), static_cast<float>(_itemHeight));
            this->setContentSize(s);
        }
    }
}

// AtlasNode - CocosNodeTexture protocol

const BlendFunc& AtlasNode::getBlendFunc() const
{
    return _blendFunc;
}

void AtlasNode::setBlendFunc(const BlendFunc& blendFunc)
{
    _blendFunc = blendFunc;
}

void AtlasNode::updateBlendFunc()
{
    if (!_textureAtlas->getTexture()->hasPremultipliedAlpha())
    {
        _blendFunc = BlendFunc::ALPHA_NON_PREMULTIPLIED;
        setOpacityModifyRGB(false);
    }
    else
    {
        _blendFunc = BlendFunc::ALPHA_PREMULTIPLIED;
        setOpacityModifyRGB(true);
    }
}

void AtlasNode::setTexture(Texture2D* texture)
{
    _textureAtlas->setTexture(texture);
    updateProgramStateTexture(texture);

    this->updateBlendFunc();
    this->updateOpacityModifyRGB();
}

Texture2D* AtlasNode::getTexture() const
{
    return _textureAtlas->getTexture();
}

void AtlasNode::setTextureAtlas(TextureAtlas* textureAtlas)
{
    AX_SAFE_RETAIN(textureAtlas);
    AX_SAFE_RELEASE(_textureAtlas);
    _textureAtlas = textureAtlas;
}

TextureAtlas* AtlasNode::getTextureAtlas() const
{
    return _textureAtlas;
}

size_t AtlasNode::getQuadsToDraw() const
{
    return _quadsToDraw;
}

void AtlasNode::setQuadsToDraw(ssize_t quadsToDraw)
{
    _quadsToDraw = quadsToDraw;
}

}
