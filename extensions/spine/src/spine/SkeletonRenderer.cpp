/******************************************************************************
 * Spine Runtimes License Agreement
 * Last updated July 28, 2023. Replaces all prior versions.
 *
 * Copyright (c) 2013-2023, Esoteric Software LLC
 *
 * Integration of the Spine Runtimes into software or otherwise creating
 * derivative works of the Spine Runtimes is permitted under the terms and
 * conditions of Section 2 of the Spine Editor License Agreement:
 * http://esotericsoftware.com/spine-editor-license
 *
 * Otherwise, it is permitted to integrate the Spine Runtimes into software or
 * otherwise create derivative works of the Spine Runtimes (collectively,
 * "Products"), provided that each user of the Products must obtain their own
 * Spine Editor license and redistribution of the Products in any form must
 * include this license and copyright notice.
 *
 * THE SPINE RUNTIMES ARE PROVIDED BY ESOTERIC SOFTWARE LLC "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL ESOTERIC SOFTWARE LLC BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES,
 * BUSINESS INTERRUPTION, OR LOSS OF USE, DATA, OR PROFITS) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THE
 * SPINE RUNTIMES, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#include <algorithm>
#include <spine/Extension.h>
#include <spine/spine-axmol.h>

using namespace ax;

namespace spine {

	namespace {
		AxmolTextureLoader textureLoader;

		int computeTotalCoordCount(Skeleton &skeleton, int startSlotIndex, int endSlotIndex);
		axmol::Rect computeBoundingRect(const float *coords, int vertexCount);
		void interleaveCoordinates(float *dst, const float *src, int vertexCount, int dstStride);
		BlendFunc makeBlendFunc(BlendMode blendMode, bool premultipliedAlpha);
		void transformWorldVertices(float *dstCoord, int coordCount, Skeleton &skeleton, int startSlotIndex, int endSlotIndex);
		bool cullRectangle(Renderer *renderer, const Mat4 &transform, const axmol::Rect &rect);
		Color4B ColorToColor4B(const Color &color);
		bool slotIsOutRange(Slot &slot, int startSlotIndex, int endSlotIndex);
		bool nothingToDraw(Slot &slot, int startSlotIndex, int endSlotIndex);
	}// namespace

// C Variable length array
#ifdef _MSC_VER
// VLA not supported, use _malloca
#define VLA(type, arr, count) \
	type *arr = static_cast<type *>(_malloca(sizeof(type) * count))
#define VLA_FREE(arr) \
	do { _freea(arr); } while (false)
#else
#define VLA(type, arr, count) \
	type arr[count]
#define VLA_FREE(arr)
#endif

	SkeletonRenderer *SkeletonRenderer::createWithSkeleton(Skeleton *skeleton, bool ownsSkeleton, bool ownsSkeletonData) {
		SkeletonRenderer *node = new SkeletonRenderer(skeleton, ownsSkeleton, ownsSkeletonData);
		node->autorelease();
		return node;
	}

	SkeletonRenderer *SkeletonRenderer::createWithData(SkeletonData *skeletonData, bool ownsSkeletonData) {
		SkeletonRenderer *node = new SkeletonRenderer(skeletonData, ownsSkeletonData);
		node->autorelease();
		return node;
	}

	SkeletonRenderer *SkeletonRenderer::createWithFile(const std::string &skeletonDataFile, Atlas *atlas, float scale) {
		SkeletonRenderer *node = new SkeletonRenderer(skeletonDataFile, atlas, scale);
		node->autorelease();
		return node;
	}

	SkeletonRenderer *SkeletonRenderer::createWithFile(const std::string &skeletonDataFile, const std::string &atlasFile, float scale) {
		SkeletonRenderer *node = new SkeletonRenderer(skeletonDataFile, atlasFile, scale);
		node->autorelease();
		return node;
	}

	void SkeletonRenderer::initialize() {
		_clipper = new (__FILE__, __LINE__) SkeletonClipping();

		_blendFunc = BlendFunc::ALPHA_PREMULTIPLIED;
		setOpacityModifyRGB(true);

		setTwoColorTint(false);

		_skeleton->setToSetupPose();
		_skeleton->updateWorldTransform();
	}

	void SkeletonRenderer::setupGLProgramState(bool /*twoColorTintEnabled*/) {
	}

	void SkeletonRenderer::setSkeletonData(SkeletonData *skeletonData, bool ownsSkeletonData) {
		_skeleton = new (__FILE__, __LINE__) Skeleton(skeletonData);
		_ownsSkeletonData = ownsSkeletonData;
	}

	SkeletonRenderer::SkeletonRenderer()
		: _atlas(nullptr), _attachmentLoader(nullptr), _timeScale(1), _debugSlots(false), _debugBones(false), _debugMeshes(false), _debugBoundingRect(false), _startSlotIndex(0), _endSlotIndex(std::numeric_limits<int>::max()) {
	}

	SkeletonRenderer::SkeletonRenderer(Skeleton *skeleton, bool ownsSkeleton, bool ownsSkeletonData, bool ownsAtlas)
		: _atlas(nullptr), _attachmentLoader(nullptr), _timeScale(1), _debugSlots(false), _debugBones(false), _debugMeshes(false), _debugBoundingRect(false), _startSlotIndex(0), _endSlotIndex(std::numeric_limits<int>::max()) {
		initWithSkeleton(skeleton, ownsSkeleton, ownsSkeletonData, ownsAtlas);
	}

	SkeletonRenderer::SkeletonRenderer(SkeletonData *skeletonData, bool ownsSkeletonData)
		: _atlas(nullptr), _attachmentLoader(nullptr), _timeScale(1), _debugSlots(false), _debugBones(false), _debugMeshes(false), _debugBoundingRect(false), _startSlotIndex(0), _endSlotIndex(std::numeric_limits<int>::max()) {
		initWithData(skeletonData, ownsSkeletonData);
	}

	SkeletonRenderer::SkeletonRenderer(const std::string &skeletonDataFile, Atlas *atlas, float scale)
		: _atlas(nullptr), _attachmentLoader(nullptr), _timeScale(1), _debugSlots(false), _debugBones(false), _debugMeshes(false), _debugBoundingRect(false), _startSlotIndex(0), _endSlotIndex(std::numeric_limits<int>::max()) {
		initWithJsonFile(skeletonDataFile, atlas, scale);
	}

	SkeletonRenderer::SkeletonRenderer(const std::string &skeletonDataFile, const std::string &atlasFile, float scale)
		: _atlas(nullptr), _attachmentLoader(nullptr), _timeScale(1), _debugSlots(false), _debugBones(false), _debugMeshes(false), _debugBoundingRect(false), _startSlotIndex(0), _endSlotIndex(std::numeric_limits<int>::max()) {
		initWithJsonFile(skeletonDataFile, atlasFile, scale);
	}

	SkeletonRenderer::~SkeletonRenderer() {
		if (_ownsSkeletonData) delete _skeleton->getData();
		if (_ownsSkeleton) delete _skeleton;
		if (_ownsAtlas && _atlas) delete _atlas;
		if (_attachmentLoader) delete _attachmentLoader;
		delete _clipper;
	}

	void SkeletonRenderer::initWithSkeleton(Skeleton *skeleton, bool ownsSkeleton, bool ownsSkeletonData, bool ownsAtlas) {
		_skeleton = skeleton;
		_ownsSkeleton = ownsSkeleton;
		_ownsSkeletonData = ownsSkeletonData;
		_ownsAtlas = ownsAtlas;
		initialize();
	}

	void SkeletonRenderer::initWithData(SkeletonData *skeletonData, bool ownsSkeletonData) {
		_ownsSkeleton = true;
		setSkeletonData(skeletonData, ownsSkeletonData);
		initialize();
	}

	void SkeletonRenderer::initWithJsonFile(const std::string &skeletonDataFile, Atlas *atlas, float scale) {
		_atlas = atlas;
		_attachmentLoader = new (__FILE__, __LINE__) AxmolAtlasAttachmentLoader(_atlas);

		SkeletonJson json(_attachmentLoader);
		json.setScale(scale);
		SkeletonData *skeletonData = json.readSkeletonDataFile(skeletonDataFile.c_str());
		AXASSERT(skeletonData, (!json.getError().isEmpty() ? json.getError().buffer() : "Error reading skeleton data."));

		_ownsSkeleton = true;
		setSkeletonData(skeletonData, true);

		initialize();
	}

	void SkeletonRenderer::initWithJsonFile(const std::string &skeletonDataFile, const std::string &atlasFile, float scale) {
		_atlas = new (__FILE__, __LINE__) Atlas(atlasFile.c_str(), &textureLoader, true);
		AXASSERT(_atlas, "Error reading atlas file.");

		_attachmentLoader = new (__FILE__, __LINE__) AxmolAtlasAttachmentLoader(_atlas);

		SkeletonJson json(_attachmentLoader);
		json.setScale(scale);
		SkeletonData *skeletonData = json.readSkeletonDataFile(skeletonDataFile.c_str());
		AXASSERT(skeletonData, (!json.getError().isEmpty() ? json.getError().buffer() : "Error reading skeleton data."));

		_ownsSkeleton = true;
		_ownsAtlas = true;
		setSkeletonData(skeletonData, true);

		initialize();
	}

	void SkeletonRenderer::initWithBinaryFile(const std::string &skeletonDataFile, Atlas *atlas, float scale) {
		_atlas = atlas;
		_attachmentLoader = new (__FILE__, __LINE__) AxmolAtlasAttachmentLoader(_atlas);

		SkeletonBinary binary(_attachmentLoader);
		binary.setScale(scale);
		SkeletonData *skeletonData = binary.readSkeletonDataFile(skeletonDataFile.c_str());
		AXASSERT(skeletonData, (!binary.getError().isEmpty() ? binary.getError().buffer() : "Error reading skeleton data."));
		_ownsSkeleton = true;
		setSkeletonData(skeletonData, true);

		initialize();
	}

	void SkeletonRenderer::initWithBinaryFile(const std::string &skeletonDataFile, const std::string &atlasFile, float scale) {
		_atlas = new (__FILE__, __LINE__) Atlas(atlasFile.c_str(), &textureLoader, true);
		AXASSERT(_atlas, "Error reading atlas file.");

		_attachmentLoader = new (__FILE__, __LINE__) AxmolAtlasAttachmentLoader(_atlas);

		SkeletonBinary binary(_attachmentLoader);
		binary.setScale(scale);
		SkeletonData *skeletonData = binary.readSkeletonDataFile(skeletonDataFile.c_str());
		AXASSERT(skeletonData, (!binary.getError().isEmpty() ? binary.getError().buffer() : "Error reading skeleton data."));
		_ownsSkeleton = true;
		_ownsAtlas = true;
		setSkeletonData(skeletonData, true);

		initialize();
	}


	void SkeletonRenderer::update(float deltaTime) {
		Node::update(deltaTime);
	}

	void SkeletonRenderer::draw(Renderer *renderer, const Mat4 &transform, uint32_t transformFlags) {
		// Early exit if the skeleton is invisible.
		if (getDisplayedOpacity() == 0 || _skeleton->getColor().a == 0) {
			return;
		}

		const int coordCount = computeTotalCoordCount(*_skeleton, _startSlotIndex, _endSlotIndex);
		if (coordCount == 0) {
			return;
		}
		assert(coordCount % 2 == 0);

		VLA(float, worldCoords, coordCount);
		transformWorldVertices(worldCoords, coordCount, *_skeleton, _startSlotIndex, _endSlotIndex);

#if AX_USE_CULLING
		const axmol::Rect bb = computeBoundingRect(worldCoords, coordCount / 2);

		if (cullRectangle(renderer, transform, bb)) {
			VLA_FREE(worldCoords);
			return;
		}
#endif

		const float *worldCoordPtr = worldCoords;
		SkeletonBatch *batch = SkeletonBatch::getInstance();
		SkeletonTwoColorBatch *twoColorBatch = SkeletonTwoColorBatch::getInstance();
		const bool hasSingleTint = (isTwoColorTint() == false);

		const Color3B displayedColor = getDisplayedColor();
		Color nodeColor;
		nodeColor.r = displayedColor.r / 255.f;
		nodeColor.g = displayedColor.g / 255.f;
		nodeColor.b = displayedColor.b / 255.f;
		nodeColor.a = getDisplayedOpacity() / 255.f;

		Color color;
		Color darkColor;
		const float darkPremultipliedAlpha = _premultipliedAlpha ? 1.f : 0;
		TwoColorTrianglesCommand *lastTwoColorTrianglesCommand = nullptr;
		for (int i = 0, n = (int)_skeleton->getSlots().size(); i < n; ++i) {
			Slot *slot = _skeleton->getDrawOrder()[i];

			if (nothingToDraw(*slot, _startSlotIndex, _endSlotIndex)) {
				_clipper->clipEnd(*slot);
				continue;
			}

			axmol::TrianglesCommand::Triangles triangles;
			TwoColorTriangles trianglesTwoColor;
            static unsigned short quadIndices[6] = {0, 1, 2, 2, 3, 0};
            Texture2D *texture = nullptr;

			if (slot->getAttachment()->getRTTI().isExactly(RegionAttachment::rtti)) {
				RegionAttachment *attachment = static_cast<RegionAttachment *>(slot->getAttachment());
				texture = (Texture2D*)((AtlasRegion*)attachment->getRegion())->page->texture;

				float *dstTriangleVertices = nullptr;
				int dstStride = 0;// in floats
				if (hasSingleTint) {
					triangles.indices = quadIndices;
					triangles.indexCount = 6;
					triangles.verts = batch->allocateVertices(4);
					triangles.vertCount = 4;
					assert(triangles.vertCount == 4);
                    for (int v = 0, i = 0; v < triangles.vertCount; v++, i += 2) {
                        auto &texCoords = triangles.verts[v].texCoords;
                        texCoords.u = attachment->getUVs()[i];
                        texCoords.v = attachment->getUVs()[i + 1];
                    }
					dstStride = sizeof(V3F_C4B_T2F) / sizeof(float);
					dstTriangleVertices = reinterpret_cast<float *>(triangles.verts);
				} else {
					trianglesTwoColor.indices = quadIndices;
					trianglesTwoColor.indexCount = 6;
					trianglesTwoColor.verts = twoColorBatch->allocateVertices(4);
					trianglesTwoColor.vertCount = 4;
					assert(trianglesTwoColor.vertCount == 4);
                    for (int v = 0, i = 0; v < trianglesTwoColor.vertCount; v++, i += 2) {
                        auto &texCoords = trianglesTwoColor.verts[v].texCoords;
                        texCoords.u = attachment->getUVs()[i];
                        texCoords.v = attachment->getUVs()[i + 1];
                    }
					dstTriangleVertices = reinterpret_cast<float *>(trianglesTwoColor.verts);
					dstStride = sizeof(V3F_C4B_C4B_T2F) / sizeof(float);
				}
				// Copy world vertices to triangle vertices.
				interleaveCoordinates(dstTriangleVertices, worldCoordPtr, 4, dstStride);
				worldCoordPtr += 8;

				color = attachment->getColor();
			} else if (slot->getAttachment()->getRTTI().isExactly(MeshAttachment::rtti)) {
				MeshAttachment *attachment = (MeshAttachment *) slot->getAttachment();
				texture = (Texture2D*)((AtlasRegion*)attachment->getRegion())->page->texture;

				float *dstTriangleVertices = nullptr;
				int dstStride = 0;// in floats
				int dstVertexCount = 0;
				if (hasSingleTint) {
					triangles.indices = attachment->getTriangles().buffer();
					triangles.indexCount = (unsigned short)attachment->getTriangles().size();
					triangles.verts = batch->allocateVertices((int)attachment->getWorldVerticesLength() / 2);
					triangles.vertCount = (int)attachment->getWorldVerticesLength() / 2;
                    for (int v = 0, i = 0; v < triangles.vertCount; v++, i += 2) {
                        auto &texCoords = triangles.verts[v].texCoords;
                        texCoords.u = attachment->getUVs()[i];
                        texCoords.v = attachment->getUVs()[i + 1];
                    }
					dstTriangleVertices = (float *) triangles.verts;
					dstStride = sizeof(V3F_C4B_T2F) / sizeof(float);
					dstVertexCount = triangles.vertCount;
				} else {
					trianglesTwoColor.indices = attachment->getTriangles().buffer();
					trianglesTwoColor.indexCount = (unsigned short)attachment->getTriangles().size();
					trianglesTwoColor.verts = twoColorBatch->allocateVertices((int)attachment->getWorldVerticesLength() / 2);
					trianglesTwoColor.vertCount = (int)attachment->getWorldVerticesLength() / 2;
                    for (int v = 0, i = 0; v < trianglesTwoColor.vertCount; v++, i += 2) {
                        auto &texCoords = trianglesTwoColor.verts[v].texCoords;
                        texCoords.u = attachment->getUVs()[i];
                        texCoords.v = attachment->getUVs()[i + 1];
                    }
					dstTriangleVertices = (float *) trianglesTwoColor.verts;
					dstStride = sizeof(V3F_C4B_C4B_T2F) / sizeof(float);
					dstVertexCount = trianglesTwoColor.vertCount;
				}

				// Copy world vertices to triangle vertices.
				//assert(dstVertexCount * 2 == attachment->super.worldVerticesLength);
				interleaveCoordinates(dstTriangleVertices, worldCoordPtr, dstVertexCount, dstStride);
				worldCoordPtr += dstVertexCount * 2;

				color = attachment->getColor();
			} else if (slot->getAttachment()->getRTTI().isExactly(ClippingAttachment::rtti)) {
				ClippingAttachment *clip = (ClippingAttachment *) slot->getAttachment();
				_clipper->clipStart(*slot, clip);
				continue;
			} else {
				_clipper->clipEnd(*slot);
				continue;
			}

			if (slot->hasDarkColor()) {
				darkColor = slot->getDarkColor();
			} else {
				darkColor.r = 0;
				darkColor.g = 0;
				darkColor.b = 0;
			}
			darkColor.a = darkPremultipliedAlpha;

			color.a *= nodeColor.a * _skeleton->getColor().a * slot->getColor().a;
			if (color.a == 0) {
				_clipper->clipEnd(*slot);
				continue;
			}
			color.r *= nodeColor.r * _skeleton->getColor().r * slot->getColor().r;
			color.g *= nodeColor.g * _skeleton->getColor().g * slot->getColor().g;
			color.b *= nodeColor.b * _skeleton->getColor().b * slot->getColor().b;
			if (_premultipliedAlpha) {
				color.r *= color.a;
				color.g *= color.a;
				color.b *= color.a;
			}

			const axmol::Color4B color4B = ColorToColor4B(color);
			const axmol::Color4B darkColor4B = ColorToColor4B(darkColor);
			const BlendFunc blendFunc = makeBlendFunc(slot->getData().getBlendMode(), texture->hasPremultipliedAlpha());
			_blendFunc = blendFunc;

			if (hasSingleTint) {
				if (_clipper->isClipping()) {
					_clipper->clipTriangles((float *) &triangles.verts[0].vertices, triangles.indices, triangles.indexCount, (float *) &triangles.verts[0].texCoords, sizeof(axmol::V3F_C4B_T2F) / 4);
					batch->deallocateVertices(triangles.vertCount);

					if (_clipper->getClippedTriangles().size() == 0) {
						_clipper->clipEnd(*slot);
						continue;
					}

					triangles.vertCount = (int)_clipper->getClippedVertices().size() / 2;
					triangles.verts = batch->allocateVertices(triangles.vertCount);
					triangles.indexCount = (int)_clipper->getClippedTriangles().size();
					triangles.indices = batch->allocateIndices(triangles.indexCount);
					memcpy(triangles.indices, _clipper->getClippedTriangles().buffer(), sizeof(unsigned short) * _clipper->getClippedTriangles().size());

                    const float* verts  = _clipper->getClippedVertices().buffer();
                    const float* uvs    = _clipper->getClippedUVs().buffer();
                    V3F_C4B_T2F* vertex = triangles.verts;
                    for (int v = 0, vn = triangles.vertCount, vv = 0; v < vn;
                         ++v, vv += 2, ++vertex)
                    {
                        vertex->vertices.x  = verts[vv];
                        vertex->vertices.y  = verts[vv + 1];
                        vertex->texCoords.u = uvs[vv];
                        vertex->texCoords.v = uvs[vv + 1];
                        vertex->colors      = color4B;
                    }
					batch->addCommand(renderer, _globalZOrder, texture, _programState, blendFunc, triangles, transform, transformFlags);
				} else {
					// Not clipping.
                    V3F_C4B_T2F* vertex = triangles.verts;
                    for (int v = 0, vn = triangles.vertCount; v < vn;
                         ++v, ++vertex)
                    {
                        vertex->colors = color4B;
                    }
					batch->addCommand(renderer, _globalZOrder, texture, _programState, blendFunc, triangles, transform, transformFlags);
				}
			} else {
				// Two color tinting.

				if (_clipper->isClipping()) {
					_clipper->clipTriangles((float *) &trianglesTwoColor.verts[0].position, trianglesTwoColor.indices, trianglesTwoColor.indexCount, (float *) &trianglesTwoColor.verts[0].texCoords, sizeof(V3F_C4B_C4B_T2F) / 4);
					twoColorBatch->deallocateVertices(trianglesTwoColor.vertCount);

					if (_clipper->getClippedTriangles().size() == 0) {
						_clipper->clipEnd(*slot);
						continue;
					}

					trianglesTwoColor.vertCount = (int)_clipper->getClippedVertices().size() / 2;
					trianglesTwoColor.verts = twoColorBatch->allocateVertices(trianglesTwoColor.vertCount);
					trianglesTwoColor.indexCount = (int)_clipper->getClippedTriangles().size();
					trianglesTwoColor.indices = twoColorBatch->allocateIndices(trianglesTwoColor.indexCount);
					memcpy(trianglesTwoColor.indices, _clipper->getClippedTriangles().buffer(), sizeof(unsigned short) * _clipper->getClippedTriangles().size());

                    const float* verts = _clipper->getClippedVertices().buffer();
                    const float* uvs   = _clipper->getClippedUVs().buffer();

                    V3F_C4B_C4B_T2F* vertex = trianglesTwoColor.verts;
                    for (int v = 0, vn = trianglesTwoColor.vertCount, vv = 0; v < vn;
                         ++v, vv += 2, ++vertex)
                    {
                        vertex->position.x  = verts[vv];
                        vertex->position.y  = verts[vv + 1];
                        vertex->texCoords.u = uvs[vv];
                        vertex->texCoords.v = uvs[vv + 1];
                        vertex->color       = color4B;
                        vertex->color2      = darkColor4B;
                    }
					lastTwoColorTrianglesCommand = twoColorBatch->addCommand(renderer, _globalZOrder, texture, _programState, blendFunc, trianglesTwoColor, transform, transformFlags);
				} else {
                    V3F_C4B_C4B_T2F* vertex = trianglesTwoColor.verts;
                    for (int v = 0, vn = trianglesTwoColor.vertCount; v < vn;
                         ++v, ++vertex)
                    {
                        vertex->color  = color4B;
                        vertex->color2 = darkColor4B;
                    }
					lastTwoColorTrianglesCommand = twoColorBatch->addCommand(renderer, _globalZOrder, texture, _programState, blendFunc, trianglesTwoColor, transform, transformFlags);
				}
			}
			_clipper->clipEnd(*slot);
		}
		_clipper->clipEnd();

		if (lastTwoColorTrianglesCommand) {
			Node *parent = this->getParent();

			// We need to decide if we can postpone flushing the current batch. We can postpone if the next sibling node is a two color
			// tinted skeleton with the same global-z.
			// The parent->getChildrenCount() > 100 check is a hack as checking for a sibling is an O(n) operation, and if all children
			// of this nodes parent are skeletons, we are in O(n2) territory.
			if (!parent || parent->getChildrenCount() > 100 || getChildrenCount() != 0) {
				lastTwoColorTrianglesCommand->setForceFlush(true);
			} else {
				const axmol::Vector<Node *> &children = parent->getChildren();
				Node *sibling = nullptr;
				for (ssize_t i = 0; i < children.size(); i++) {
					if (children.at(i) == this) {
						if (i < children.size() - 1) {
							sibling = children.at(i + 1);
							break;
						}
					}
				}
				if (!sibling) {
					lastTwoColorTrianglesCommand->setForceFlush(true);
				} else {
					SkeletonRenderer *siblingSkeleton = dynamic_cast<SkeletonRenderer *>(sibling);
					if (!siblingSkeleton ||                                               // flush is next sibling isn't a SkeletonRenderer
						!siblingSkeleton->isTwoColorTint() ||                             // flush if next sibling isn't two color tinted
						!siblingSkeleton->isVisible() ||                                  // flush if next sibling is two color tinted but not visible
						(siblingSkeleton->getGlobalZOrder() != this->getGlobalZOrder())) {// flush if next sibling is two color tinted but z-order differs
						lastTwoColorTrianglesCommand->setForceFlush(true);
					}
				}
			}
		}

		if (_debugBoundingRect || _debugSlots || _debugBones || _debugMeshes) {
			drawDebug(renderer, transform, transformFlags);
		}

		VLA_FREE(worldCoords);
	}


	void SkeletonRenderer::drawDebug(Renderer *renderer, const Mat4 &transform, uint32_t transformFlags) {

#if !defined(USE_MATRIX_STACK_PROJECTION_ONLY)
		Director *director = Director::getInstance();
		director->pushMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_MODELVIEW);
		director->loadMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_MODELVIEW, transform);
#endif

		DrawNode *drawNode = DrawNode::create();
		drawNode->setGlobalZOrder(getGlobalZOrder());

		// Draw bounding rectangle
		if (_debugBoundingRect) {
			drawNode->setLineWidth(2.0f);
			const axmol::Rect brect = getBoundingBox();
			const Vec2 points[4] =
					{
							brect.origin,
							{brect.origin.x + brect.size.width, brect.origin.y},
							{brect.origin.x + brect.size.width, brect.origin.y + brect.size.height},
							{brect.origin.x, brect.origin.y + brect.size.height}};
			drawNode->drawPoly(points, 4, true, Color4F::GREEN);
		}

		if (_debugSlots) {
			// Slots.
			// DrawPrimitives::setDrawColor4B(0, 0, 255, 255);
			drawNode->setLineWidth(2.0f);
			V3F_C4B_T2F_Quad quad;
			for (int i = 0, n = (int)_skeleton->getSlots().size(); i < n; i++) {
				Slot *slot = _skeleton->getDrawOrder()[i];

				if (!slot->getBone().isActive()) continue;
				if (!slot->getAttachment() || !slot->getAttachment()->getRTTI().isExactly(RegionAttachment::rtti)) continue;

				if (slotIsOutRange(*slot, _startSlotIndex, _endSlotIndex)) {
					continue;
				}

				RegionAttachment *attachment = (RegionAttachment *) slot->getAttachment();
				float worldVertices[8];
				attachment->computeWorldVertices(*slot, worldVertices, 0, 2);
				const Vec2 points[4] =
						{
								{worldVertices[0], worldVertices[1]},
								{worldVertices[2], worldVertices[3]},
								{worldVertices[4], worldVertices[5]},
								{worldVertices[6], worldVertices[7]}};
				drawNode->drawPoly(points, 4, true, Color4F::BLUE);
			}
		}

		if (_debugBones) {
			// Bone lengths.
			drawNode->setLineWidth(2.0f);
			for (int i = 0, n = (int)_skeleton->getBones().size(); i < n; i++) {
				Bone *bone = _skeleton->getBones()[i];
				if (!bone->isActive()) continue;
				float x = bone->getData().getLength() * bone->getA() + bone->getWorldX();
				float y = bone->getData().getLength() * bone->getC() + bone->getWorldY();
				drawNode->drawLine(Vec2(bone->getWorldX(), bone->getWorldY()), Vec2(x, y), Color4F::RED);
			}
			// Bone origins.
			auto color = Color4F::BLUE;// Root bone is blue.
			for (int i = 0, n = (int)_skeleton->getBones().size(); i < n; i++) {
				Bone *bone = _skeleton->getBones()[i];
				if (!bone->isActive()) continue;
				drawNode->drawPoint(Vec2(bone->getWorldX(), bone->getWorldY()), 4, color);
				if (i == 0) color = Color4F::GREEN;
			}
		}

		if (_debugMeshes) {
			// Meshes.
			drawNode->setLineWidth(2.0f);
			for (int i = 0, n = (int)_skeleton->getSlots().size(); i < n; ++i) {
				Slot *slot = _skeleton->getDrawOrder()[i];
				if (!slot->getBone().isActive()) continue;
				if (!slot->getAttachment() || !slot->getAttachment()->getRTTI().isExactly(MeshAttachment::rtti)) continue;
				MeshAttachment *const mesh = static_cast<MeshAttachment *>(slot->getAttachment());
				VLA(float, worldCoord, mesh->getWorldVerticesLength());
				mesh->computeWorldVertices(*slot, 0, mesh->getWorldVerticesLength(), worldCoord, 0, 2);
				for (size_t t = 0; t < mesh->getTriangles().size(); t += 3) {
					// Fetch triangle indices
					const int idx0 = mesh->getTriangles()[t + 0];
					const int idx1 = mesh->getTriangles()[t + 1];
					const int idx2 = mesh->getTriangles()[t + 2];
					const Vec2 v[3] =
							{
									worldCoord + (idx0 * 2),
									worldCoord + (idx1 * 2),
									worldCoord + (idx2 * 2)};
					drawNode->drawPoly(v, 3, true, Color4F::YELLOW);
				}
				VLA_FREE(worldCoord);
			}
		}

		drawNode->draw(renderer, transform, transformFlags);
#if !defined(USE_MATRIX_STACK_PROJECTION_ONLY)
		director->popMatrix(MATRIX_STACK_TYPE::MATRIX_STACK_MODELVIEW);
#endif
	}

	axmol::Rect SkeletonRenderer::getBoundingBox() const {
		const int coordCount = computeTotalCoordCount(*_skeleton, _startSlotIndex, _endSlotIndex);
		if (coordCount == 0) return {0, 0, 0, 0};
		VLA(float, worldCoords, coordCount);
		transformWorldVertices(worldCoords, coordCount, *_skeleton, _startSlotIndex, _endSlotIndex);
		const axmol::Rect bb = computeBoundingRect(worldCoords, coordCount / 2);
		VLA_FREE(worldCoords);
		return bb;
	}

	// --- Convenience methods for Skeleton_* functions.

	void SkeletonRenderer::updateWorldTransform() {
		_skeleton->updateWorldTransform();
	}

	void SkeletonRenderer::setToSetupPose() {
		_skeleton->setToSetupPose();
	}
	void SkeletonRenderer::setBonesToSetupPose() {
		_skeleton->setBonesToSetupPose();
	}
	void SkeletonRenderer::setSlotsToSetupPose() {
		_skeleton->setSlotsToSetupPose();
	}

	Bone *SkeletonRenderer::findBone(const std::string &boneName) const {
		return _skeleton->findBone(boneName.c_str());
	}

	Slot *SkeletonRenderer::findSlot(const std::string &slotName) const {
		return _skeleton->findSlot(slotName.c_str());
	}

	void SkeletonRenderer::setSkin(const std::string &skinName) {
		_skeleton->setSkin(skinName.empty() ? 0 : skinName.c_str());
	}
	void SkeletonRenderer::setSkin(const char *skinName) {
		_skeleton->setSkin(skinName);
	}

	Attachment *SkeletonRenderer::getAttachment(const std::string &slotName, const std::string &attachmentName) const {
		return _skeleton->getAttachment(slotName.c_str(), attachmentName.c_str());
	}
	bool SkeletonRenderer::setAttachment(const std::string &slotName, const std::string &attachmentName) {
		bool result = _skeleton->getAttachment(slotName.c_str(), attachmentName.empty() ? 0 : attachmentName.c_str()) ? true : false;
		_skeleton->setAttachment(slotName.c_str(), attachmentName.empty() ? 0 : attachmentName.c_str());
		return result;
	}
	bool SkeletonRenderer::setAttachment(const std::string &slotName, const char *attachmentName) {
		bool result = _skeleton->getAttachment(slotName.c_str(), attachmentName) ? true : false;
		_skeleton->setAttachment(slotName.c_str(), attachmentName);
		return result;
	}

	void SkeletonRenderer::setTwoColorTint(bool enabled) {
		_twoColorTint = enabled;
		setupGLProgramState(enabled);
	}

	bool SkeletonRenderer::isTwoColorTint() {
		return _twoColorTint;
	}

	void SkeletonRenderer::setSlotsRange(int startSlotIndex, int endSlotIndex) {
		_startSlotIndex = startSlotIndex == -1 ? 0 : startSlotIndex;
		_endSlotIndex = endSlotIndex == -1 ? std::numeric_limits<int>::max() : endSlotIndex;
	}

	Skeleton *SkeletonRenderer::getSkeleton() const {
		return _skeleton;
	}

	void SkeletonRenderer::setTimeScale(float scale) {
		_timeScale = scale;
	}
	float SkeletonRenderer::getTimeScale() const {
		return _timeScale;
	}

	void SkeletonRenderer::setDebugSlotsEnabled(bool enabled) {
		_debugSlots = enabled;
	}
	bool SkeletonRenderer::getDebugSlotsEnabled() const {
		return _debugSlots;
	}

	void SkeletonRenderer::setDebugBonesEnabled(bool enabled) {
		_debugBones = enabled;
	}
	bool SkeletonRenderer::getDebugBonesEnabled() const {
		return _debugBones;
	}

	void SkeletonRenderer::setDebugMeshesEnabled(bool enabled) {
		_debugMeshes = enabled;
	}
	bool SkeletonRenderer::getDebugMeshesEnabled() const {
		return _debugMeshes;
	}

	void SkeletonRenderer::setDebugBoundingRectEnabled(bool enabled) {
		_debugBoundingRect = enabled;
	}

	bool SkeletonRenderer::getDebugBoundingRectEnabled() const {
		return _debugBoundingRect;
	}

	void SkeletonRenderer::onEnter() {
		Node::onEnter();
		scheduleUpdate();
	}

	void SkeletonRenderer::onExit() {
		Node::onExit();
		unscheduleUpdate();
	}

	// --- CCBlendProtocol

	const BlendFunc &SkeletonRenderer::getBlendFunc() const {
		return _blendFunc;
	}

	void SkeletonRenderer::setBlendFunc(const BlendFunc &blendFunc) {
		_blendFunc = blendFunc;
	}

	void SkeletonRenderer::setOpacityModifyRGB(bool value) {
		_premultipliedAlpha = value;
	}

	bool SkeletonRenderer::isOpacityModifyRGB() const {
		return _premultipliedAlpha;
	}

	namespace {
		axmol::Rect computeBoundingRect(const float *coords, int vertexCount) {
			assert(coords);
			assert(vertexCount > 0);

			const float *v = coords;
			float minX = v[0];
			float minY = v[1];
			float maxX = minX;
			float maxY = minY;
			for (int i = 1; i < vertexCount; ++i) {
				v += 2;
				float x = v[0];
				float y = v[1];
				minX = std::min(minX, x);
				minY = std::min(minY, y);
				maxX = std::max(maxX, x);
				maxY = std::max(maxY, y);
			}
			return {minX, minY, maxX - minX, maxY - minY};
		}

		bool slotIsOutRange(Slot &slot, int startSlotIndex, int endSlotIndex) {
			const int index = slot.getData().getIndex();
			return startSlotIndex > index || endSlotIndex < index;
		}

		bool nothingToDraw(Slot &slot, int startSlotIndex, int endSlotIndex) {
			Attachment *attachment = slot.getAttachment();
			if (!attachment ||
				slotIsOutRange(slot, startSlotIndex, endSlotIndex) ||
				!slot.getBone().isActive())
				return true;
			const auto &attachmentRTTI = attachment->getRTTI();
			if (attachmentRTTI.isExactly(ClippingAttachment::rtti))
				return false;
			if (slot.getColor().a == 0)
				return true;
			if (attachmentRTTI.isExactly(RegionAttachment::rtti)) {
				if (static_cast<RegionAttachment *>(attachment)->getColor().a == 0)
					return true;
			} else if (attachmentRTTI.isExactly(MeshAttachment::rtti)) {
				if (static_cast<MeshAttachment *>(attachment)->getColor().a == 0)
					return true;
			}
			return false;
		}

		int computeTotalCoordCount(Skeleton &skeleton, int startSlotIndex, int endSlotIndex) {
			int coordCount = 0;
			for (size_t i = 0; i < skeleton.getSlots().size(); ++i) {
				Slot &slot = *skeleton.getSlots()[i];
				if (nothingToDraw(slot, startSlotIndex, endSlotIndex)) {
					continue;
				}
				Attachment *const attachment = slot.getAttachment();
				if (attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
					coordCount += 8;
				} else if (attachment->getRTTI().isExactly(MeshAttachment::rtti)) {
					MeshAttachment *const mesh = static_cast<MeshAttachment *>(attachment);
					coordCount += mesh->getWorldVerticesLength();
				}
			}
			return coordCount;
		}


		void transformWorldVertices(float *dstCoord, int coordCount, Skeleton &skeleton, int startSlotIndex, int endSlotIndex) {
			float *dstPtr = dstCoord;
#ifndef NDEBUG
			float *const dstEnd = dstCoord + coordCount;
#endif
			for (size_t i = 0; i < skeleton.getSlots().size(); ++i) {
				/*const*/ Slot &slot = *skeleton.getDrawOrder()[i];// match the draw order of SkeletonRenderer::Draw
				if (nothingToDraw(slot, startSlotIndex, endSlotIndex)) {
					continue;
				}
				Attachment *const attachment = slot.getAttachment();
				if (attachment->getRTTI().isExactly(RegionAttachment::rtti)) {
					RegionAttachment *const regionAttachment = static_cast<RegionAttachment *>(attachment);
					assert(dstPtr + 8 <= dstEnd);
					regionAttachment->computeWorldVertices(slot, dstPtr, 0, 2);
					dstPtr += 8;
				} else if (attachment->getRTTI().isExactly(MeshAttachment::rtti)) {
					MeshAttachment *const mesh = static_cast<MeshAttachment *>(attachment);
					assert(dstPtr + mesh->getWorldVerticesLength() <= dstEnd);
					mesh->computeWorldVertices(slot, 0, mesh->getWorldVerticesLength(), dstPtr, 0, 2);
					dstPtr += mesh->getWorldVerticesLength();
				}
			}
			assert(dstPtr == dstEnd);
		}

		void interleaveCoordinates(float *dst, const float *src, int count, int dstStride) {
			if (dstStride == 2) {
				memcpy(dst, src, sizeof(float) * count * 2);
			} else {
				for (int i = 0; i < count; ++i) {
					dst[0] = src[0];
					dst[1] = src[1];
					dst += dstStride;
					src += 2;
				}
			}
		}

		BlendFunc makeBlendFunc(BlendMode blendMode, bool premultipliedAlpha) {
			BlendFunc blendFunc;
			switch (blendMode) {
				case BlendMode_Additive:
					blendFunc.src = premultipliedAlpha ? backend::BlendFactor::ONE : backend::BlendFactor::SRC_ALPHA;
					blendFunc.dst = backend::BlendFactor::ONE;
					break;
				case BlendMode_Multiply:
					blendFunc.src = backend::BlendFactor::DST_COLOR;
					blendFunc.dst = backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
					break;
				case BlendMode_Screen:
					blendFunc.src = backend::BlendFactor::ONE;
					blendFunc.dst = backend::BlendFactor::ONE_MINUS_SRC_COLOR;
					break;
				default:
					blendFunc.src = premultipliedAlpha ? backend::BlendFactor::ONE : backend::BlendFactor::SRC_ALPHA;
					blendFunc.dst = backend::BlendFactor::ONE_MINUS_SRC_ALPHA;
			}
			return blendFunc;
		}


		bool cullRectangle(Renderer *renderer, const Mat4 &transform, const axmol::Rect &rect) {
			if (Camera::getVisitingCamera() == nullptr)
				return false;

			auto director = Director::getInstance();
			auto scene = director->getRunningScene();

			if (!scene || (scene && Camera::getDefaultCamera() != Camera::getVisitingCamera()))
				return false;

			Rect visibleRect(director->getVisibleOrigin(), director->getVisibleSize());

			// transform center point to screen space
			float hSizeX = rect.size.width / 2;
			float hSizeY = rect.size.height / 2;
			Vec3 v3p(rect.origin.x + hSizeX, rect.origin.y + hSizeY, 0);
			transform.transformPoint(&v3p);
			Vec2 v2p = Camera::getVisitingCamera()->projectGL(v3p);

			// convert content size to world coordinates
			float wshw = std::max(fabsf(hSizeX * transform.m[0] + hSizeY * transform.m[4]), fabsf(hSizeX * transform.m[0] - hSizeY * transform.m[4]));
			float wshh = std::max(fabsf(hSizeX * transform.m[1] + hSizeY * transform.m[5]), fabsf(hSizeX * transform.m[1] - hSizeY * transform.m[5]));

			// enlarge visible rect half size in screen coord
			visibleRect.origin.x -= wshw;
			visibleRect.origin.y -= wshh;
			visibleRect.size.width += wshw * 2;
			visibleRect.size.height += wshh * 2;
			return !visibleRect.containsPoint(v2p);
		}


		Color4B ColorToColor4B(const Color &color) {
			return {(uint8_t) (color.r * 255.f), (uint8_t) (color.g * 255.f), (uint8_t) (color.b * 255.f), (uint8_t) (color.a * 255.f)};
		}
	}// namespace

}// namespace spine
