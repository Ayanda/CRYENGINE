// Copyright 2001-2016 Crytek GmbH / Crytek Group. All rights reserved.

#ifndef D3D_RENDER_AUX_GEOM_H
#define D3D_RENDER_AUX_GEOM_H

#include "../Common/RenderAuxGeom.h"

#if defined(ENABLE_RENDER_AUX_GEOM)

class CD3D9Renderer;
class ICrySizer;

class CRenderAuxGeomD3D : public IRenderAuxGeomImpl
{
public:
	virtual void Flush(SAuxGeomCBRawDataPackaged& data, size_t begin, size_t end, bool reset = false);
	virtual void RT_Flush(SAuxGeomCBRawDataPackaged& data, size_t begin, size_t end, bool reset = false);

	virtual void DrawStringImmediate(IFFont_RenderProxy* pFont, float x, float y, float z, const char* pStr, const bool asciiMultiLine, const STextDrawContext& ctx);

	virtual void FlushTextMessages(CTextMessages& tMessages, bool reset);

	void         Process();

public:
	static CRenderAuxGeomD3D* Create(CD3D9Renderer& renderer)
	{
		return(new CRenderAuxGeomD3D(renderer));
	}

public:
	~CRenderAuxGeomD3D();

	void        FreeMemory();

	CAuxGeomCB* GetRenderAuxGeom(void* jobID = 0);
	int         GetDeviceDataSize();
	void        ReleaseDeviceObjects();
	HRESULT     RestoreDeviceObjects();
	void        SetOrthoMode(bool enable, Matrix44A* pMatrix = 0);
	void        GetMemoryUsage(ICrySizer* pSizer) const;
	void        ReleaseShader() { SAFE_RELEASE_FORCE(m_pAuxGeomShader); }

	void*       operator new(size_t s)
	{
		uint8* p = (uint8*) malloc(s + 16 + 8);
		memset(p, 0, s + 16 + 8);
		uint8* pRet = (uint8*) ((size_t) (p + 16 + 8) & ~0xF);
		((uint8**) pRet)[-1] = p;
		return pRet;
	}

	void operator delete(void* p)
	{
		free(((uint8**)p)[-1]);
	}

private:
	struct SDrawObjMesh
	{
		SDrawObjMesh()
			: m_numVertices(0)
			, m_numFaces(0)
			, m_pVB(~0u)
			, m_pIB(~0u)
		{
		}

		~SDrawObjMesh()
		{
			Release();
		}

		void Release();

		int GetDeviceDataSize() const;

		uint32           m_numVertices;
		uint32           m_numFaces;
		buffer_handle_t m_pVB;
		buffer_handle_t m_pIB;
	};

	enum EAuxObjNumLOD
	{
		e_auxObjNumLOD = 5
	};

	struct SMatrices
	{
		SMatrices()
			: m_pCurTransMat(0)
		{
			m_matView.SetIdentity();
			m_matViewInv.SetIdentity();
			m_matProj.SetIdentity();
			m_matTrans3D.SetIdentity();

			m_matTrans2D = Matrix44A(2, 0, 0, 0,
			                         0, -2, 0, 0,
			                         0, 0, 0, 0,
			                         -1, 1, 0, 1);
		}

		void UpdateMatrices(CD3D9Renderer& renderer);

		Matrix44A        m_matView;
		Matrix44A        m_matViewInv;
		Matrix44A        m_matProj;
		Matrix44A        m_matTrans3D;
		Matrix44A        m_matTrans2D;
		const Matrix44A* m_pCurTransMat;
	};

	class CAuxGeomCBCollector
	{
		class SThread;

		typedef std::map<threadID, SThread*> AUXThreadMap;
		typedef std::vector<SThread*>        AUXThreads;
		typedef std::vector<CAuxGeomCB*>     AUXJobs;

		class SThread
		{
			typedef std::map<void*, CAuxGeomCB*> AUXJobMap;

			CAuxGeomCB*       m_cbCurrent;
			AUXJobMap         m_auxJobMap;

			mutable CryRWLock m_rwlLocal;

		public:
			SThread() : m_cbCurrent() {}

			CAuxGeomCB* Get(IRenderAuxGeomImpl* pRenderAuxGeomImpl, void* jobID, threadID tid)
			{
				if (jobID == 0 && m_cbCurrent)
				{
					return m_cbCurrent;
				}

				m_rwlLocal.RLock();

				AUXJobMap::const_iterator it = m_auxJobMap.find(jobID);
				CAuxGeomCB* pAuxGeomCB = m_auxJobMap.end() != it ? it->second : 0;

				m_rwlLocal.RUnlock();

				if (!pAuxGeomCB)
				{
					threadID mainThreadID, renderThreadID;

					gRenDev->GetThreadIDs(mainThreadID, renderThreadID);

					if (tid == renderThreadID) pAuxGeomCB = new CAuxGeomCB(pRenderAuxGeomImpl);
					else                       pAuxGeomCB = new CAuxGeomCBWorkerThread(pRenderAuxGeomImpl);

					m_rwlLocal.WLock();
					m_auxJobMap.insert(AUXJobMap::value_type(jobID, pAuxGeomCB));
					m_rwlLocal.WUnlock();
				}

				return pAuxGeomCB;
			}

			~SThread()
			{
				for (AUXJobMap::iterator cbit = m_auxJobMap.begin(); cbit != m_auxJobMap.end(); ++cbit)
				{
					delete cbit->second;
				}
			}

			void Process(AUXJobs& auxJobs)
			{
				m_rwlLocal.RLock();

				for (SThread::AUXJobMap::const_iterator job = m_auxJobMap.begin(); job != m_auxJobMap.end(); ++job)
				{
					auxJobs.push_back(job->second);
				}

				m_rwlLocal.RUnlock();
			}

			void FreeMemory()
			{
				m_rwlLocal.WLock();
				for (AUXJobMap::const_iterator job = m_auxJobMap.begin(); job != m_auxJobMap.end(); ++job)
				{
					// MUST BE called after final CAuxGeomCB::Commit()
					// adding data (issuing render commands) is not thread safe !!!
					job->second->FreeMemory();
				}
				m_rwlLocal.WUnlock();
			}

			void GetMemoryUsage(ICrySizer* pSizer) const
			{
				m_rwlLocal.RLock();
				for (AUXJobMap::const_iterator job = m_auxJobMap.begin(); job != m_auxJobMap.end(); ++job)
				{
					// MUST BE called after final CAuxGeomCB::Commit()
					// adding data (issuing render commands) is not thread safe !!!
					job->second->GetMemoryUsage(pSizer);
				}
				m_rwlLocal.RUnlock();
			}
		};

		AUXThreadMap      m_auxThreadMap;
		AUXThreads        m_tmpThreads;
		AUXJobs           m_tmpJobs;

		mutable CryRWLock m_rwGlobal;

	public:
		~CAuxGeomCBCollector()
		{
			for (AUXThreadMap::iterator cbit = m_auxThreadMap.begin(); cbit != m_auxThreadMap.end(); ++cbit)
			{
				delete cbit->second;
			}
		}

		CAuxGeomCB* Get(IRenderAuxGeomImpl* pRenderAuxGeomImpl, void* jobID)
		{
			threadID tid = CryGetCurrentThreadId();

			m_rwGlobal.RLock();

			AUXThreadMap::const_iterator it = m_auxThreadMap.find(tid);
			SThread* auxThread = m_auxThreadMap.end() != it ? it->second : 0;

			m_rwGlobal.RUnlock();

			if (!auxThread)
			{
				auxThread = new SThread;

				m_rwGlobal.WLock();
				m_auxThreadMap.insert(AUXThreadMap::value_type(tid, auxThread));
				m_rwGlobal.WUnlock();
			}

			return auxThread->Get(pRenderAuxGeomImpl, jobID, tid);
		}

		void FreeMemory()
		{
			m_rwGlobal.WLock();
			for (AUXThreadMap::const_iterator cbit = m_auxThreadMap.begin(); cbit != m_auxThreadMap.end(); ++cbit)
			{
				cbit->second->FreeMemory();
			}
			m_rwGlobal.WUnlock();
		}

		void Process()
		{
			FUNCTION_PROFILER_RENDERER

			m_rwGlobal.RLock();
			for (AUXThreadMap::const_iterator it = m_auxThreadMap.begin(); it != m_auxThreadMap.end(); ++it)
			{
				m_tmpThreads.push_back(it->second);
			}
			m_rwGlobal.RUnlock();

			for (AUXThreads::iterator it = m_tmpThreads.begin(); it != m_tmpThreads.end(); ++it)
			{
				(*it)->Process(m_tmpJobs);
			}

			for (AUXJobs::iterator job = m_tmpJobs.begin(); job != m_tmpJobs.end(); ++job)
			{
				(*job)->Process();
			}

			m_tmpThreads.clear();
			m_tmpJobs.clear();
		}

		void GetMemoryUsage(ICrySizer* pSizer) const
		{
			m_rwGlobal.RLock();
			for (AUXThreadMap::const_iterator it = m_auxThreadMap.begin(); it != m_auxThreadMap.end(); ++it)
			{
				it->second->GetMemoryUsage(pSizer);
			}
			m_rwGlobal.RUnlock();
		}
	};

private:
	CRenderAuxGeomD3D(CD3D9Renderer& renderer);


	CRenderPrimitive& PreparePrimitive(const SAuxGeomRenderFlags& flags, const CCryNameTSCRC& techique, ERenderPrimitiveType topology, EVertexFormat format, size_t stride, buffer_handle_t vb, buffer_handle_t ib, const Matrix44* mViewProj);

	void DrawAuxPrimitives(CAuxGeomCB::AuxSortedPushBuffer::const_iterator itBegin, CAuxGeomCB::AuxSortedPushBuffer::const_iterator itEnd, const Matrix44& mViewProj);
	void DrawAuxIndexedPrimitives(CAuxGeomCB::AuxSortedPushBuffer::const_iterator itBegin, CAuxGeomCB::AuxSortedPushBuffer::const_iterator itEnd, const Matrix44& mViewProj);
	void DrawAuxObjects(CAuxGeomCB::AuxSortedPushBuffer::const_iterator itBegin, CAuxGeomCB::AuxSortedPushBuffer::const_iterator itEnd, const Matrix44& mViewProj);

	void PrepareThickLines2D(CAuxGeomCB::AuxSortedPushBuffer::const_iterator itBegin, CAuxGeomCB::AuxSortedPushBuffer::const_iterator itEnd);
	void PrepareThickLines3D(CAuxGeomCB::AuxSortedPushBuffer::const_iterator itBegin, CAuxGeomCB::AuxSortedPushBuffer::const_iterator itEnd);

	void PrepareRendering();
	void Prepare(const SAuxGeomRenderFlags& renderFlags, Matrix44A& mat);

	template<typename TMeshFunc>
	HRESULT                                  CreateMesh(SDrawObjMesh& mesh, TMeshFunc meshFunc);

	const Matrix44A&                         GetCurrentView() const;
	const Matrix44A&                         GetCurrentViewInv() const;
	const Matrix44A&                         GetCurrentProj() const;
	const Matrix44A&                         GetCurrentTrans3D() const;
	const Matrix44A&                         GetCurrentTrans2D() const;

	bool                                     IsOrthoMode() const;
	bool                                     HasWorldMatrix() const;

	const CAuxGeomCB::AuxVertexBuffer&       GetAuxVertexBuffer() const;
	const CAuxGeomCB::AuxIndexBuffer&        GetAuxIndexBuffer() const;
	const CAuxGeomCB::AuxDrawObjParamBuffer& GetAuxDrawObjParamBuffer() const;
	const Matrix44A&                         GetAuxOrthoMatrix(int idx) const;
	const Matrix34&                          GetAuxWorldMatrix(int idx) const;

private:

	class CBufferManager
	{
		buffer_handle_t vbAux = ~0u;
		buffer_handle_t ibAux = ~0u;

		static buffer_handle_t fill(buffer_handle_t buf, BUFFER_BIND_TYPE type, const void* data, size_t size);
		static buffer_handle_t update                   (BUFFER_BIND_TYPE type, const void* data, size_t size);

	public:
		~CBufferManager();

		void FillVB(const void* src, size_t size) { vbAux = fill(vbAux, BBT_VERTEX_BUFFER, src, size); }
		void FillIB(const void* src, size_t size) { ibAux = fill(ibAux, BBT_INDEX_BUFFER,  src, size); }

		buffer_handle_t GetVB() { return vbAux; }
		buffer_handle_t GetIB() { return ibAux; }
	};

	CD3D9Renderer&                            m_renderer;

	CBufferManager                                   m_bufman;
	CPrimitiveRenderPass                             m_geomPass;
	std::map<ERenderPrimitiveType, CRenderPrimitive> m_geomPrimitiveCache;

	uint32                                    m_wndXRes;
	uint32                                    m_wndYRes;
	float                                     m_aspect;
	float                                     m_aspectInv;

	SMatrices                                 m_matrices;

	CAuxGeomCB::EPrimType                     m_curPrimType;

	uint8                                     m_curPointSize;

	int                                       m_curTransMatrixIdx;
	int                                       m_curWorldMatrixIdx;

	CShader*                                  m_pAuxGeomShader;
	EAuxGeomPublicRenderflags_DrawInFrontMode m_curDrawInFrontMode;

	CAuxGeomCB::AuxSortedPushBuffer           m_auxSortedPushBuffer;
	const CAuxGeomCB::SAuxGeomCBRawData*      m_pCurCBRawData;
	CAuxGeomCBCollector                       m_auxGeomCBCol;

	int                                       CV_r_auxGeom;

	SDrawObjMesh                              m_sphereObj[e_auxObjNumLOD];
	SDrawObjMesh                              m_coneObj[e_auxObjNumLOD];
	SDrawObjMesh                              m_cylinderObj[e_auxObjNumLOD];
};


#endif // #if defined(ENABLE_RENDER_AUX_GEOM)

#endif // D3D_RENDER_AUX_GEOM_H
